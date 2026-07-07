// snd::audio::loadMediaAudio -- pull the audio track out of any media file
// AVFoundation can read (mp4/mov/m4v video, m4a/aac audio...). Channel count
// and sample rate are preserved (5.1 film audio arrives as 6 channels).
// Compiled with ARC.

#include "snd/audio.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

namespace snd::audio {

bool loadMediaAudio(const std::string& path, Buffer& out, std::string* error)
{
    @autoreleasepool {
        NSURL* url =
            [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        AVAssetTrack* track =
            [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];
        if (!track) {
            if (error)
                *error = "no audio track in " + path;
            return false;
        }

        // the track's native layout
        uint32_t channels = 2;
        double rate = 48000.0;
        CMAudioFormatDescriptionRef fmt =
            (__bridge CMAudioFormatDescriptionRef)track.formatDescriptions.firstObject;
        if (fmt) {
            const AudioStreamBasicDescription* asbd =
                CMAudioFormatDescriptionGetStreamBasicDescription(fmt);
            if (asbd) {
                if (asbd->mChannelsPerFrame > 0)
                    channels = asbd->mChannelsPerFrame;
                if (asbd->mSampleRate > 0)
                    rate = asbd->mSampleRate;
            }
        }

        NSError* nserr = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset
                                                              error:&nserr];
        if (!reader) {
            if (error)
                *error = nserr ? nserr.localizedDescription.UTF8String
                               : "could not read " + path;
            return false;
        }

        NSDictionary* settings = @{
            AVFormatIDKey : @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey : @32,
            AVLinearPCMIsFloatKey : @YES,
            AVLinearPCMIsBigEndianKey : @NO,
            AVLinearPCMIsNonInterleaved : @NO,
            AVSampleRateKey : @(rate),
            AVNumberOfChannelsKey : @(channels),
        };
        AVAssetReaderTrackOutput* output =
            [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
                                                       outputSettings:settings];
        if (![reader canAddOutput:output]) {
            if (error)
                *error = "unsupported audio track in " + path;
            return false;
        }
        [reader addOutput:output];
        if (![reader startReading]) {
            if (error)
                *error = "could not start reading " + path;
            return false;
        }

        out.channels = channels;
        out.sampleRate = (uint32_t)rate;
        out.samples.clear();

        while (true) {
            CMSampleBufferRef sb = [output copyNextSampleBuffer];
            if (!sb)
                break;
            CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
            if (bb) {
                size_t len = CMBlockBufferGetDataLength(bb);
                size_t n = len / sizeof(float);
                size_t old = out.samples.size();
                out.samples.resize(old + n);
                CMBlockBufferCopyDataBytes(bb, 0, len, out.samples.data() + old);
            }
            CFRelease(sb);
        }

        if (reader.status != AVAssetReaderStatusCompleted) {
            if (error)
                *error = "decode failed partway through " + path;
            return false;
        }
        // trim to whole frames, just in case
        out.samples.resize(out.samples.size() - out.samples.size() % channels);
        if (out.samples.empty()) {
            if (error)
                *error = "audio track was empty in " + path;
            return false;
        }
        return true;
    }
}

} // namespace snd::audio
