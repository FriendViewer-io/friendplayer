/* copyright (c) 2013 jean-marc valin */
/*
   redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   this software is provided by the copyright holders and contributors
   ``as is'' and any express or implied warranties, including, but not
   limited to, the implied warranties of merchantability and fitness for
   a particular purpose are disclaimed. in no event shall the copyright owner
   or contributors be liable for any direct, indirect, incidental, special,
   exemplary, or consequential damages (including, but not limited to,
   procurement of substitute goods or services; loss of use, data, or
   profits; or business interruption) however caused and on any theory of
   liability, whether in contract, strict liability, or tort (including
   negligence or otherwise) arising in any way out of the use of this
   software, even if advised of the possibility of such damage.
*/

/* this is meant to be a simple example of encoding and decoding audio
   using opus. it should make it easy to understand how the opus api
   works. for more information, see the full api documentation at:
   https://www.opus-codec.org/docs/ */

#include <WinSock2.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include "wasapi_capture.hh"
#include <opus/opus.h>


   /*the frame size is hardcoded for this sample code but it doesn't have to be*/
#define FRAME_SIZE 480
#define SAMPLE_RATE 48000
#define CHANNELS 2
#define APPLICATION OPUS_APPLICATION_AUDIO
#define BITRATE 64000

#define MAX_FRAME_SIZE 100*FRAME_SIZE
#define MAX_PACKET_SIZE 4800

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000


#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);


OpusDecoder* decoder;
OpusEncoder* encoder;
FILE* fin;
FILE* fout;
unsigned char* pcm_bytes;
void getdata(int num_frames, BYTE* pData) {
    opus_int16 in[FRAME_SIZE * CHANNELS];
    opus_int16 out[MAX_FRAME_SIZE * CHANNELS];
    unsigned char cbits[MAX_PACKET_SIZE];
    int nbBytes;
    int err;
    int i;
    int frame_size;
    int offset = 0;
    int iter = 0;

    /* Read a 16 bits/sample audio frame. */
    /* Convert from little-endian ordering. */
    while (offset < num_frames * 4) {
        //fread(pcm_bytes, 4, , fin);
        fread(pcm_bytes, sizeof(short) * CHANNELS, FRAME_SIZE, fin);
        /*if (feof(fin))
            break;*/
        for (i = 0; i < CHANNELS * FRAME_SIZE; i++)
            in[i] = pcm_bytes[2 * i + 1] << 8 | pcm_bytes[2 * i];

        /* Encode the frame. */
        nbBytes = opus_encode(encoder, in, FRAME_SIZE, cbits, MAX_PACKET_SIZE);
        if (nbBytes < 0)
        {
            fprintf(stderr, "encode failed: %s\n", opus_strerror(nbBytes));
            return;
        }


        /* Decode the data. In this example, frame_size will be constant because
            the encoder is using a constant frame size. However, that may not
            be the case for all encoders, so the decoder must always check
            the frame size returned. */
        frame_size = opus_decode(decoder, cbits, nbBytes, out, MAX_FRAME_SIZE, 0);
        //std::cout << "Num frames decoded = " << frame_size << std::endl;
        if (frame_size < 0)
        {
            fprintf(stderr, "decoder failed: %s\n", opus_strerror(frame_size));
            return;
        }

        /* Convert to little-endian ordering. */
        for (i = 0; i < CHANNELS * frame_size; i++)
        {
            pData[2 * i + offset] = out[i] & 0xFF;
            pData[2 * i + 1 + offset] = (out[i] >> 8) & 0xFF;
        }
        //fwrite(pData + offset, sizeof(short), frame_size * CHANNELS, fout);
        offset += frame_size * 4;
        /*if (iter == 4) {
            fclose(fout);
            break;
        }
        iter++;*/
    }
    std::cout << "Offset = " << offset << " wanted = " << (num_frames * 4) << std::endl;
    /* Write the decoded audio to file. */

}
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")

inline AVSampleFormat GetSampleFormat(const WAVEFORMATEX* wave_format)
{
    switch (wave_format->wFormatTag) {
    case WAVE_FORMAT_PCM:
        if (16 == wave_format->wBitsPerSample) {
            return AV_SAMPLE_FMT_S16;
        }
        if (32 == wave_format->wBitsPerSample) {
            return AV_SAMPLE_FMT_S32;
        }
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        return AV_SAMPLE_FMT_FLT;
    case WAVE_FORMAT_ALAW:
    case WAVE_FORMAT_MULAW:
        return AV_SAMPLE_FMT_U8;
    case WAVE_FORMAT_EXTENSIBLE:
    {
        const WAVEFORMATEXTENSIBLE* wfe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave_format);
        if (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT == wfe->SubFormat) {
            return AV_SAMPLE_FMT_FLT;
        }
        if (KSDATAFORMAT_SUBTYPE_PCM == wfe->SubFormat) {
            if (16 == wave_format->wBitsPerSample) {
                return AV_SAMPLE_FMT_S16;
            }
            if (32 == wave_format->wBitsPerSample) {
                return AV_SAMPLE_FMT_S32;
            }
        }
        break;
    }
    default:
        break;
    }
    return AV_SAMPLE_FMT_NONE;
}

HRESULT PlayAudioStream()
{
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME hnsActualDuration;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioClient* pAudioClient = NULL;
    IAudioRenderClient* pRenderClient = NULL;
    WAVEFORMATEX* pwfx = NULL;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    UINT32 numFramesPadding;
    BYTE* pData;
    DWORD flags = 0;
    size_t read;
    WAVEFORMATEXTENSIBLE* wfext;

    
    CoInitialize(nullptr);
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)

        hr = pEnumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr)

        hr = pDevice->Activate(
            IID_IAudioClient, CLSCTX_ALL,
            NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)
   
        hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr)



    /*        wfext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
    
        if (wfext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        {
            wfext->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            wfext->Format.wBitsPerSample = 16;
            wfext->Format.nBlockAlign =
                wfext->Format.nChannels *
                wfext->Format.wBitsPerSample /
                8;
            wfext->Format.nAvgBytesPerSec =
                wfext->Format.nSamplesPerSec *
                wfext->Format.nBlockAlign;
            wfext->Samples.wValidBitsPerSample =
                wfext->Format.wBitsPerSample;
        }*/
        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            hnsRequestedDuration,
            0,
            pwfx,
            NULL);
    EXIT_ON_ERROR(hr)

        // Tell the audio source which format to use.
        //hr = pMySource->SetFormat(pwfx);
    EXIT_ON_ERROR(hr)

        // Get the actual size of the allocated buffer.
        hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

        hr = pAudioClient->GetService(
            IID_IAudioRenderClient,
            (void**)&pRenderClient);
    EXIT_ON_ERROR(hr)

        // Grab the entire buffer for the initial fill operation.
        hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
    EXIT_ON_ERROR(hr)

        // Load the initial data into the shared buffer.
        read = fread(pData, 2, bufferFrameCount, fin);
        //getdata(bufferFrameCount, pData);
        //std::cout << "Read " << recv(sock, (char*)pData, bufferFrameCount * 4, 0);
    EXIT_ON_ERROR(hr)

        hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
    EXIT_ON_ERROR(hr)

        // Calculate the actual duration of the allocated buffer.
        hnsActualDuration = (double)REFTIMES_PER_SEC *
        bufferFrameCount / pwfx->nSamplesPerSec;

    hr = pAudioClient->Start();  // Start playing.
    EXIT_ON_ERROR(hr)

    // Each loop fills about half of the shared buffer.
    while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
    {
        // Sleep for half the buffer duration.
        Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

        // See how much buffer space is available.
        hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        EXIT_ON_ERROR(hr)

            numFramesAvailable = bufferFrameCount - numFramesPadding;

        // Grab all the available space in the shared buffer.
        hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
        EXIT_ON_ERROR(hr)

            // Get next 1/2-second of data from the audio source.
            //hr = pMySource->LoadData(numFramesAvailable, pData, &flags);
            //getdata(numFramesAvailable, pData);
            //std::cout << "Read " << recv(sock, (char*)pData, numFramesAvailable * 4, 0);
            fread(pData, 2, numFramesAvailable, fin);
        EXIT_ON_ERROR(hr)

            hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
        EXIT_ON_ERROR(hr)
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

    hr = pAudioClient->Stop();  // Stop playing.
    EXIT_ON_ERROR(hr)

        Exit:
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator)
        SAFE_RELEASE(pDevice)
        SAFE_RELEASE(pAudioClient)
        SAFE_RELEASE(pRenderClient)

        return hr;
}

void EncodeAudio() {

    for (i = 0; i < CHANNELS * FRAME_SIZE; i++)
        in[i] = pcm_bytes[2 * i + 1] << 8 | pcm_bytes[2 * i];

    /* Encode the frame. */
    nbBytes = opus_encode(encoder, in, FRAME_SIZE, cbits, MAX_PACKET_SIZE);
}


int main(int argc, char** argv)
{
    int err;
    encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &err);
    if (err < 0)
    {
        fprintf(stderr, "failed to create an encoder: %s\n", opus_strerror(err));
        return 0;
    }
    err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE));
    decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    fin = fopen("output", "rb");
    fout = fopen("enc2", "wb");
    pcm_bytes = (unsigned char*)malloc(192000);
    fin = fopen("single", "rb");
    //fout = fopen("pcm_convert", "wb");
    //getdata(0, 0);
    //fclose(fout);
    PlayAudioStream();
    /*av_get_default_channel_layout(2);

    const uint8_t* buffer = (uint8_t*)malloc(192000);
    const uint8_t** buf = &buffer;

    SwrContext* context;
    context = swr_alloc_set_opts(
        nullptr, av_get_default_channel_layout(2), AV_SAMPLE_FMT_S16,
        48000, av_get_default_channel_layout(1), AV_SAMPLE_FMT_FLT, 48000,
        0, 0);
    swr_init(context);
    while (!feof(fin)) {
        int num_samples = fread((void*)buffer, 4, 24000, fin);
        uint8_t* output;
        int out_s = av_rescale_rnd(swr_get_delay(context, 48000) + num_samples/2, 48000, 48000, AV_ROUND_DOWN);
        av_samples_alloc(&output, NULL, 2, out_s, AV_SAMPLE_FMT_S16, 0);
        out_s = swr_convert(context, &output, out_s, buf, num_samples);
        fwrite(output, 4, out_s, fout);
        av_freep(&output);
    }
    fclose(fout);
    swr_free(&context);
    */

    /*
    WasapiCapture capture;
    capture.init();
    capture.toggle_copy();
    Sleep(5000);
    capture.toggle_copy();
    std::vector<uint8_t> buf;
    fout = fopen("single", "wb");
    while (capture.copy_buffers(buf)) {
        //std::vector<short> d;
        //for (int i = 0; i < buf.size() / 2; ++i) {
        //    float sample = ((float*)buf.data())[i];
        //    d.push_back(static_cast<short>(sample * 32767));
        //}
        //memcpy(d.data(), buf.data(), buf.size());
        
        fwrite(buf.data(), 1, buf.size(), fout);
    }
    fclose(fout);*/

    /*if (strcmp(argv[1], "1") == 0) {
        WasapiCapture capture;
        capture.init();
        capture.toggle_copy();
        WasapiCapture::capture_thread(&capture);
    }
    else {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        SOCKADDR_IN          saddr;
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(5959);
        saddr.sin_addr.s_addr = inet_addr("23.119.123.146");//saddr.sin_addr.s_addr = inet_addr("96.244.149.47");
        std::cout << "Write " << connect(sock, (SOCKADDR*)&saddr, sizeof(saddr)) << std::endl;
        PlayAudioStream(sock);
    }*/


    
    //PlayAudioStream();
    /*char* infile;
    file* fin;

    char* outfile;
    file* fout;

    unsigned char cbits[max_packet_size];
    int nbbytes;
    /*holds the state of the encoder and decoder */
    //opusencoder* encoder;
    //opusdecoder* decoder;
    //int err;

    //if (argc != 3)
    //{
    //    fprintf(stderr, "usage: trivial_example input.pcm output.pcm\n");
    //    fprintf(stderr, "input and output are 16-bit little-endian raw files\n");
    //    return exit_failure;
    //}

    ///*create a new encoder state */
    //encoder = opus_encoder_create(sample_rate, channels, application, &err);
    //if (err < 0)
    //{
    //    fprintf(stderr, "failed to create an encoder: %s\n", opus_strerror(err));
    //    return exit_failure;
    //}
    ///* set the desired bit-rate. you can also set other parameters if needed.
    //   the opus library is designed to have good defaults, so only set
    //   parameters you know you need. doing otherwise is likely to result
    //   in worse quality, but better. */
    //err = opus_encoder_ctl(encoder, opus_set_bitrate(bitrate));
    //if (err < 0)
    //{
    //    fprintf(stderr, "failed to set bitrate: %s\n", opus_strerror(err));
    //    return exit_failure;
    //}
    //infile = argv[1];
    //fin = fopen(infile, "r");
    //if (fin == null)
    //{
    //    fprintf(stderr, "failed to open input file: %s\n", strerror(errno));
    //    return exit_failure;
    //}

    
    ///* create a new decoder state. */
    //decoder = opus_decoder_create(sample_rate, channels, &err);
    //if (err < 0)
    //{
    //    fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(err));
    //    return exit_failure;
    //}
    //outfile = argv[2];
    //fout = fopen(outfile, "w");
    //if (fout == null)
    //{
    //    fprintf(stderr, "failed to open output file: %s\n", strerror(errno));
    //    return exit_failure;
    //}

    //while (1)
    //{
    //    int i;
    //    unsigned char pcm_bytes[max_frame_size * channels * 2];
    //    int frame_size;

    //    /* read a 16 bits/sample audio frame. */
    //    fread(pcm_bytes, sizeof(short) * channels, frame_size, fin);
    //    if (feof(fin))
    //        break;
    //    /* convert from little-endian ordering. */
    //    for (i = 0;i < channels * frame_size;i++)
    //        in[i] = pcm_bytes[2 * i + 1] << 8 | pcm_bytes[2 * i];

    //    /* encode the frame. */
    //    nbbytes = opus_encode(encoder, in, frame_size, cbits, max_packet_size);
    //    if (nbbytes < 0)
    //    {
    //        fprintf(stderr, "encode failed: %s\n", opus_strerror(nbbytes));
    //        return exit_failure;
    //    }


    //    /* decode the data. in this example, frame_size will be constant because
    //       the encoder is using a constant frame size. however, that may not
    //       be the case for all encoders, so the decoder must always check
    //       the frame size returned. */
    //    frame_size = opus_decode(decoder, cbits, nbbytes, out, max_frame_size, 0);
    //    if (frame_size < 0)
    //    {
    //        fprintf(stderr, "decoder failed: %s\n", opus_strerror(frame_size));
    //        return exit_failure;
    //    }

    //    /* convert to little-endian ordering. */
    //    for (i = 0;i < channels * frame_size;i++)
    //    {
    //        pcm_bytes[2 * i] = out[i] & 0xff;
    //        pcm_bytes[2 * i + 1] = (out[i] >> 8) & 0xff;
    //    }
    //    /* write the decoded audio to file. */
    //    fwrite(pcm_bytes, sizeof(short), frame_size * channels, fout);
    //}
    ///*destroy the encoder state*/
    //opus_encoder_destroy(encoder);
    //opus_decoder_destroy(decoder);
    //fclose(fin);
    //fclose(fout);
    //return exit_success;
}
