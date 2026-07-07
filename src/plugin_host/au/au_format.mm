// AU (Audio Unit v2) backend for snd::plugin. macOS only.
//
// Uses AudioToolbox/AudioComponent directly -- no third-party SDK. Discovery
// goes through the OS component registry (AudioComponentFindNext), not the
// filesystem, per the JUCE audit. v1 hosts effects ('aufx') for headless
// processing; AUv3 async instantiation is out of scope until it's needed.

#include "snd/plugin_host.h"
#include "../editor_window.h"

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace snd::plugin {

namespace {

std::string fourCC(OSType t)
{
    char s[5] = {(char)(t >> 24), (char)(t >> 16), (char)(t >> 8), (char)t, 0};
    return s;
}

OSType fourCCFromString(const std::string& s)
{
    if (s.size() != 4)
        return 0;
    return ((OSType)(unsigned char)s[0] << 24) | ((OSType)(unsigned char)s[1] << 16) |
           ((OSType)(unsigned char)s[2] << 8) | (OSType)(unsigned char)s[3];
}

std::string cfToString(CFStringRef ref)
{
    if (!ref)
        return {};
    char buf[512] = {};
    CFStringGetCString(ref, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

// identifier = "type,subtype,manufacturer" as four-char codes
std::string makeIdentifier(const AudioComponentDescription& d)
{
    return fourCC(d.componentType) + "," + fourCC(d.componentSubType) + "," +
           fourCC(d.componentManufacturer);
}

bool parseIdentifier(const std::string& id, AudioComponentDescription& out)
{
    if (id.size() != 14 || id[4] != ',' || id[9] != ',')
        return false;
    out = {};
    out.componentType = fourCCFromString(id.substr(0, 4));
    out.componentSubType = fourCCFromString(id.substr(5, 4));
    out.componentManufacturer = fourCCFromString(id.substr(10, 4));
    return out.componentType && out.componentSubType && out.componentManufacturer;
}

// ---------------------------------------------------------------------------

class AUParameter final : public Parameter {
public:
    AUParameter(AudioUnit unit, const AudioUnitParameterInfo& info, AudioUnitParameterID pid)
        : unit_(unit), pid_(pid), min_(info.minValue), max_(info.maxValue)
    {
        idString_ = std::to_string(pid);
        if (info.flags & kAudioUnitParameterFlag_HasCFNameString && info.cfNameString)
            name_ = cfToString(info.cfNameString);
        else
            name_ = info.name;
        automatable_ = (info.flags & kAudioUnitParameterFlag_NonRealTime) == 0;
        refreshCache();
    }

    const std::string& id() const override { return idString_; }
    const std::string& name() const override { return name_; }
    bool automatable() const override { return automatable_; }

    double value() const override { return cached_.load(std::memory_order_relaxed); }

    void setValue(double v) override
    {
        v = std::clamp(v, 0.0, 1.0);
        cached_.store(v, std::memory_order_relaxed);
        float native = (float)(min_ + v * (max_ - min_));
        AudioUnitSetParameter(unit_, pid_, kAudioUnitScope_Global, 0, native, 0);
    }

    void refreshCache()
    {
        AudioUnitParameterValue native = 0;
        if (AudioUnitGetParameter(unit_, pid_, kAudioUnitScope_Global, 0, &native) == noErr) {
            double norm = (max_ > min_) ? ((double)native - min_) / (max_ - min_) : 0.0;
            cached_.store(std::clamp(norm, 0.0, 1.0), std::memory_order_relaxed);
        }
    }

private:
    AudioUnit unit_;
    AudioUnitParameterID pid_;
    double min_ = 0, max_ = 1;
    std::string idString_, name_;
    bool automatable_ = true;
    std::atomic<double> cached_{0};
};

// ---------------------------------------------------------------------------

class AUInstance final : public Instance {
public:
    AUInstance(Description desc, AudioComponent comp) : desc_(std::move(desc)), component_(comp) {}

    ~AUInstance() override
    {
        closeEditor(); // view listeners must unhook before the unit dies
        unprepare();
        destroyUnit();
    }

    bool init()
    {
        if (AudioComponentInstanceNew(component_, &unit_) != noErr || !unit_)
            return false;
        buildParameterList();
        return true;
    }

    const Description& description() const override { return desc_; }

    bool prepare(double sampleRate, uint32_t maxBlockFrames) override
    {
        if (!unit_)
            return false;
        unprepare();

        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = sampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mChannelsPerFrame = channels_;
        fmt.mFramesPerPacket = 1;
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4;
        fmt.mBytesPerPacket = 4;

        // Instruments (aumu) have no input bus: setting the input format
        // fails, which is fine -- they render from MIDI, not from us.
        hasInput_ = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                         kAudioUnitScope_Input, 0, &fmt,
                                         sizeof(fmt)) == noErr;
        if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                 0, &fmt, sizeof(fmt)) != noErr)
            return false;

        UInt32 maxFrames = maxBlockFrames;
        AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice,
                             kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames));

        if (hasInput_) {
            AURenderCallbackStruct cb{};
            cb.inputProc = renderInput;
            cb.inputProcRefCon = this;
            if (AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                                     kAudioUnitScope_Input, 0, &cb, sizeof(cb)) != noErr)
                return false;
        }

        if (AudioUnitInitialize(unit_) != noErr)
            return false;

        sampleRate_ = sampleRate;
        maxBlock_ = maxBlockFrames;
        renderTime_ = 0;
        prepared_ = true;
        return true;
    }

    void unprepare() override
    {
        if (prepared_ && unit_) {
            AudioUnitUninitialize(unit_);
            prepared_ = false;
        }
    }

    bool process(const float* const* in, uint32_t inChannels,
                 float* const* out, uint32_t outChannels, uint32_t frames) override
    {
        if (!prepared_ || frames == 0 || frames > maxBlock_)
            return false;

        currentInput_ = in;
        currentInputChannels_ = inChannels;
        currentFrames_ = frames;

        // Output buffer list the AU renders into.
        std::vector<float> outStorage(channels_ * frames);
        AudioBufferList* abl =
            (AudioBufferList*)alloca(sizeof(AudioBufferList) +
                                     sizeof(AudioBuffer) * (channels_ - 1));
        abl->mNumberBuffers = channels_;
        for (uint32_t c = 0; c < channels_; ++c) {
            abl->mBuffers[c].mNumberChannels = 1;
            abl->mBuffers[c].mDataByteSize = frames * sizeof(float);
            abl->mBuffers[c].mData = outStorage.data() + c * frames;
        }

        AudioTimeStamp ts{};
        ts.mSampleTime = (Float64)renderTime_;
        ts.mFlags = kAudioTimeStampSampleTimeValid;

        AudioUnitRenderActionFlags flags = 0;
        OSStatus err = AudioUnitRender(unit_, &flags, &ts, 0, frames, abl);
        if (err != noErr)
            return false;

        for (uint32_t c = 0; c < outChannels; ++c) {
            if (!out || !out[c])
                continue;
            if (c < channels_)
                std::memcpy(out[c], abl->mBuffers[c].mData, frames * sizeof(float));
            else
                std::memset(out[c], 0, frames * sizeof(float));
        }

        renderTime_ += frames;
        return true;
    }

    bool processMidi(const float* const* in, uint32_t inChannels,
                     float* const* out, uint32_t outChannels, uint32_t frames,
                     const snd::midi::Buffer& midiIn,
                     snd::midi::Buffer* midiOut) override
    {
        if (!prepared_ || !unit_)
            return false;
        // schedule this block's events, then render
        for (auto& m : midiIn)
            MusicDeviceMIDIEvent(unit_, m.status, m.data1, m.data2, m.frame);
        (void)midiOut; // AUv2 render has no MIDI-out path
        return process(in, inChannels, out, outChannels, frames);
    }

    void idle() override
    {
        // With the editor open, the user changes parameters behind our back;
        // re-read them (throttled) so host-side sliders and saves stay true.
        if (editorWin_ && ++idleTick_ % 6 == 0)
            for (auto& owned : ownedParameters_)
                owned->refreshCache();
    }

    const std::vector<Parameter*>& parameters() const override { return parameters_; }

    Parameter* parameterById(const std::string& id) const override
    {
        auto it = paramsById_.find(id);
        return it == paramsById_.end() ? nullptr : it->second;
    }

    bool saveState(std::vector<uint8_t>& out) override
    {
        if (!unit_)
            return false;
        CFPropertyListRef props = nullptr;
        UInt32 size = sizeof(props);
        if (AudioUnitGetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
                                 0, &props, &size) != noErr || !props)
            return false;

        CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, props,
                                                  kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
        CFRelease(props);
        if (!data)
            return false;
        out.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
        CFRelease(data);
        return true;
    }

    bool loadState(const uint8_t* data, size_t size) override
    {
        if (!unit_ || !data || size == 0)
            return false;
        CFDataRef cfData = CFDataCreate(kCFAllocatorDefault, data, (CFIndex)size);
        if (!cfData)
            return false;
        CFPropertyListRef props =
            CFPropertyListCreateWithData(kCFAllocatorDefault, cfData,
                                         kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease(cfData);
        if (!props)
            return false;

        OSStatus err = AudioUnitSetProperty(unit_, kAudioUnitProperty_ClassInfo,
                                            kAudioUnitScope_Global, 0, &props, sizeof(props));
        CFRelease(props);
        if (err != noErr)
            return false;

        for (auto& owned : ownedParameters_)
            owned->refreshCache();
        return true;
    }

    // Every AU gets an editor: its own Cocoa view when it ships one, the
    // system generic parameter view (CoreAudioKit) otherwise.
    bool hasEditor() override { return unit_ != nullptr; }

    bool openEditor(const std::string& windowTitle) override
    {
        if (editorWin_)
            return true;
        if (!unit_)
            return false;

        NSView* view = createCocoaView();
        if (!view)
            view = [[[AUGenericView alloc] initWithAudioUnit:unit_] autorelease];
        if (!view)
            return false;

        int w = (int)view.frame.size.width, h = (int)view.frame.size.height;
        if (w < 80 || h < 40) {
            w = 560;
            h = 340;
        }

        editorwin::Callbacks cbs;
        cbs.onClose = [this] { closeEditor(); };
        editorWin_ = editorwin::create(windowTitle, w, h, true, cbs);
        if (!editorWin_)
            return false;
        editorwin::attachView(editorWin_, (void*)view); // window keeps it alive
        return true;
    }

    void closeEditor() override
    {
        if (!editorWin_)
            return;
        void* win = editorWin_;
        editorWin_ = nullptr;
        editorwin::destroy(win); // releases the view and its AU listeners
        for (auto& owned : ownedParameters_)
            owned->refreshCache();
    }

    bool editorOpen() const override { return editorWin_ != nullptr; }

private:
    // The AU's custom UI, if it publishes one (kAudioUnitProperty_CocoaUI).
    NSView* createCocoaView()
    {
        UInt32 size = 0;
        Boolean writable = false;
        if (AudioUnitGetPropertyInfo(unit_, kAudioUnitProperty_CocoaUI,
                                     kAudioUnitScope_Global, 0, &size,
                                     &writable) != noErr ||
            size < sizeof(AudioUnitCocoaViewInfo))
            return nil;

        auto* info = (AudioUnitCocoaViewInfo*)calloc(1, size);
        NSView* view = nil;
        if (AudioUnitGetProperty(unit_, kAudioUnitProperty_CocoaUI,
                                 kAudioUnitScope_Global, 0, info, &size) == noErr) {
            NSBundle* bundle =
                [NSBundle bundleWithURL:(NSURL*)info->mCocoaAUViewBundleLocation];
            Class cls = [bundle classNamed:(NSString*)info->mCocoaAUViewClass[0]];
            id factory = [[[cls alloc] init] autorelease];
            if (factory &&
                [factory respondsToSelector:@selector(uiViewForAudioUnit:withSize:)])
                view = [factory uiViewForAudioUnit:unit_ withSize:NSMakeSize(600, 400)];

            // the property hands us +1 references; balance them
            UInt32 numClasses =
                (UInt32)((size - sizeof(CFURLRef)) / sizeof(CFStringRef));
            if (info->mCocoaAUViewBundleLocation)
                CFRelease(info->mCocoaAUViewBundleLocation);
            for (UInt32 i = 0; i < numClasses; ++i)
                if (info->mCocoaAUViewClass[i])
                    CFRelease(info->mCocoaAUViewClass[i]);
        }
        free(info);
        return view;
    }

    static OSStatus renderInput(void* refCon, AudioUnitRenderActionFlags* /*flags*/,
                                const AudioTimeStamp* /*ts*/, UInt32 /*bus*/,
                                UInt32 frames, AudioBufferList* buffers)
    {
        auto* self = (AUInstance*)refCon;
        for (UInt32 c = 0; c < buffers->mNumberBuffers; ++c) {
            float* dst = (float*)buffers->mBuffers[c].mData;
            if (!dst)
                continue;
            if (self->currentInput_ && c < self->currentInputChannels_ &&
                self->currentInput_[c] && frames <= self->currentFrames_)
                std::memcpy(dst, self->currentInput_[c], frames * sizeof(float));
            else
                std::memset(dst, 0, frames * sizeof(float));
        }
        return noErr;
    }

    void buildParameterList()
    {
        UInt32 size = 0;
        Boolean writable = false;
        if (AudioUnitGetPropertyInfo(unit_, kAudioUnitProperty_ParameterList,
                                     kAudioUnitScope_Global, 0, &size, &writable) != noErr ||
            size == 0)
            return;

        std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
        if (AudioUnitGetProperty(unit_, kAudioUnitProperty_ParameterList,
                                 kAudioUnitScope_Global, 0, ids.data(), &size) != noErr)
            return;

        for (auto pid : ids) {
            AudioUnitParameterInfo info{};
            UInt32 infoSize = sizeof(info);
            if (AudioUnitGetProperty(unit_, kAudioUnitProperty_ParameterInfo,
                                     kAudioUnitScope_Global, pid, &info, &infoSize) != noErr)
                continue;
            auto param = std::make_unique<AUParameter>(unit_, info, pid);
            paramsById_[param->id()] = param.get();
            parameters_.push_back(param.get());
            ownedParameters_.push_back(std::move(param));
            if (info.flags & kAudioUnitParameterFlag_HasCFNameString &&
                (info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString)
                CFRelease(info.cfNameString);
        }
    }

    void destroyUnit()
    {
        parameters_.clear();
        paramsById_.clear();
        ownedParameters_.clear();
        if (unit_) {
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
    }

    Description desc_;
    AudioComponent component_ = nullptr;
    AudioUnit unit_ = nullptr;

    static constexpr uint32_t channels_ = 2;
    double sampleRate_ = 0;
    uint32_t maxBlock_ = 0;
    uint64_t renderTime_ = 0;
    bool prepared_ = false;
    bool hasInput_ = true; // false for instruments (no input bus)

    const float* const* currentInput_ = nullptr;
    uint32_t currentInputChannels_ = 0;
    uint32_t currentFrames_ = 0;

    void* editorWin_ = nullptr;
    uint32_t idleTick_ = 0;

    std::vector<std::unique_ptr<AUParameter>> ownedParameters_;
    std::vector<Parameter*> parameters_;
    std::unordered_map<std::string, Parameter*> paramsById_;
};

// ---------------------------------------------------------------------------

class AUFormat final : public Format {
public:
    const char* name() const override { return "AU"; }

    // Registry-based: no filesystem paths.
    std::vector<std::string> defaultSearchPaths() const override { return {}; }
    bool fileMightBePlugin(const std::string&) const override { return false; }

    std::vector<Description> scan(const std::string& path) override
    {
        std::vector<Description> result;
        if (!path.empty())
            return result; // registry format: only the enumerate-all call

        // Effects, music effects (aumf) and instruments (aumu). MIDI arrived
        // in SND 2026-07-07; instruments render from processMidi().
        const struct {
            OSType type;
            const char* category;
        } kinds[] = {
            {kAudioUnitType_Effect, "Effect"},
            {kAudioUnitType_MusicEffect, "MusicEffect"},
            {kAudioUnitType_MusicDevice, "Instrument"},
        };

        for (auto& kind : kinds) {
            AudioComponentDescription wanted{};
            wanted.componentType = kind.type;

            AudioComponent comp = nullptr;
            while ((comp = AudioComponentFindNext(comp, &wanted)) != nullptr) {
                AudioComponentDescription desc{};
                if (AudioComponentGetDescription(comp, &desc) != noErr)
                    continue;

                CFStringRef cfName = nullptr;
                AudioComponentCopyName(comp, &cfName);
                std::string full = cfToString(cfName);
                if (cfName)
                    CFRelease(cfName);

                // Names come as "Vendor: Name".
                std::string vendor, name = full;
                auto colon = full.find(": ");
                if (colon != std::string::npos) {
                    vendor = full.substr(0, colon);
                    name = full.substr(colon + 2);
                }

                Description d;
                d.format = "AU";
                d.identifier = makeIdentifier(desc);
                d.name = name;
                d.vendor = vendor;
                d.category = kind.category;
                result.push_back(std::move(d));
            }
        }
        return result;
    }

    std::unique_ptr<Instance> create(const Description& desc) override
    {
        AudioComponentDescription compDesc{};
        if (!parseIdentifier(desc.identifier, compDesc))
            return nullptr;

        AudioComponent comp = AudioComponentFindNext(nullptr, &compDesc);
        if (!comp)
            return nullptr;

        auto instance = std::make_unique<AUInstance>(desc, comp);
        if (!instance->init())
            return nullptr;
        return instance;
    }
};

} // namespace

std::unique_ptr<Format> createAUFormat()
{
    return std::make_unique<AUFormat>();
}

} // namespace snd::plugin
