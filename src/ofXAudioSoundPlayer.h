#pragma once

#include "ofMain.h"
#include <windows.h>
#include <xaudio2.h>
// this is a dope hack that forces the load of xaudio2 library!
// "lean-and-mean" windows doesn't include xaudio2.
#pragma comment(lib,"xaudio2.lib")
#include <synchapi.h>

//by Jay Tennant 3/8/12
//A Brief Look at XAudio2: Playing a Stream
//demonstrates streaming a wave from disk
//win32developer.com
//this code provided free, as in public domain; score!

#include "waveInfo.h"

class ofXAudioSoundPlayer : public ofBaseSoundPlayer, protected ofThread {
public:

	ofXAudioSoundPlayer(){};
	~ofXAudioSoundPlayer();
	
	bool loadSound(string fileName, bool stream = false);
	void unloadSound();
	void play();
	void stop();
	
	void setVolume(float vol);
	void setPan(float vol); // -1 = left, 1 = right
	void setSpeed(float spd);
	void setPaused(bool bP);
	void setLoop(bool bLp);
	void setMultiPlay(bool bMp);
	void setPosition(float pct); // 0 = start, 1 = end;
	void setPositionMS(int ms);

	float getPosition();
	int getPositionMS();
	bool getIsPlaying();
	float getSpeed();
	float getPan();
	bool isLoaded();
	float getVolume();

protected:

	void threadedFunction();
};

//the voice callback to let us know when the submitted buffer of the stream has finished
struct StreamingVoiceCallback : public IXAudio2VoiceCallback
{
public:
	HANDLE m_hBufferEndEvent;

	StreamingVoiceCallback() : m_hBufferEndEvent( CreateEventW( NULL, TRUE, FALSE, NULL ) ) {}
	virtual ~StreamingVoiceCallback() { CloseHandle( m_hBufferEndEvent ); }

	//overrides
    STDMETHOD_( void, OnVoiceProcessingPassStart )( UINT32 bytesRequired )
    {
    }
    STDMETHOD_( void, OnVoiceProcessingPassEnd )()
    {
    }
    STDMETHOD_( void, OnStreamEnd )()
    {
    }
    STDMETHOD_( void, OnBufferStart )( void* pContext )
    {
    }
    STDMETHOD_( void, OnBufferEnd )( void* pContext )
    {
        SetEvent( m_hBufferEndEvent );
    }
    STDMETHOD_( void, OnLoopEnd )( void* pContext )
    {
    }
    STDMETHOD_( void, OnVoiceError )( void* pContext, HRESULT error )
    {
    }
};