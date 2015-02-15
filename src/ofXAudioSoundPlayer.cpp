#include "ofXAudioSoundPlayer.h"
#include <synchapi.h>

//tells all threads it's time to quit
HANDLE g_hAbortEvent;

//XAudio2 objects
IXAudio2* g_engine = NULL;
IXAudio2MasteringVoice* g_master = NULL;

//the context to send to the StreamProc
struct StreamContext
{
	IXAudio2SourceVoice** pVoice; //the source voice that is created on the thread
	LPCTSTR szFile; //name of the file to stream
	HANDLE hVoiceLoadEvent; //lets us know the thread is set up for streaming, or encountered an error
};

// stream thread
StreamContext streamContext;
HANDLE hStreamingVoiceThread;

//the streaming thread
DWORD WINAPI StreamProc( LPVOID pContext );

//the streaming thread procedure
DWORD WINAPI StreamProc( LPVOID pContext )
{
	//required by XAudio2
	CoInitializeEx( NULL, COINIT_MULTITHREADED );

	if( pContext == NULL )
	{
		CoUninitialize();
		return -1;
	}

	StreamContext* sc = (StreamContext*)pContext;

	//instantiate the voice's callback class
	StreamingVoiceCallback callback;

	//load a file for streaming, non-buffered disk reads (no system cacheing)
	StreamingWave inFile;
	if( !inFile.load( sc->szFile ) )
	{
		ofLogError()<<"Error in file load "<<sc->szFile;
		SetEvent( sc->hVoiceLoadEvent );
		CoUninitialize();
		return -3;
	}

	//create the voice
	IXAudio2SourceVoice* source = NULL;
	if( FAILED( g_engine->CreateSourceVoice( &source, inFile.wf(), 0, 2.0f, &callback ) ) )
	{
		ofLogError()<<"Error in voice create "<<sc->szFile;
		SetEvent( sc->hVoiceLoadEvent );
		CoUninitialize();
		return -5;
	} else {
		ofLogWarning()<<"Created source voice";
	}

	//fill and queue the maximum number of buffers (except the one needed for reading new wave data)
	bool somethingsWrong = false;
	XAUDIO2_VOICE_STATE voiceState = {0};
	source->GetState( &voiceState );
	while( voiceState.BuffersQueued < STREAMINGWAVE_BUFFER_COUNT - 1 && !somethingsWrong )
	{
		//read and fill the next buffer to present
		switch( inFile.prepare() )
		{
		case StreamingWave::PR_EOF:
			//if end-of-file (or end-of-data), loop the file read
			inFile.resetFile(); //intentionally fall-through to loop sound
		case StreamingWave::PR_SUCCESS:
			//present the next available buffer
			inFile.swap();
			//submit another buffer
			source->SubmitSourceBuffer( inFile.buffer() );
			source->GetState( &voiceState );
			break;
		case StreamingWave::PR_FAILURE:
			somethingsWrong = true;
			ofLogError()<<"Something went wrong";
			break;
		}
	}

	//return the created voice through the context pointer
	sc->pVoice = &source;

	ofLogWarning()<<"Loaded";

	//signal that the voice has prepared for streaming, and ready to start
	SetEvent( sc->hVoiceLoadEvent );

	//group the events for the Wait function
	HANDLE hEvents[2] = { callback.m_hBufferEndEvent, g_hAbortEvent };

	bool quitting = false;
	while( !quitting )
	{
		//wait until either the source voice is ready for another buffer, or the abort signal is set
		DWORD eventFired = WaitForMultipleObjects( 2, hEvents, FALSE, INFINITE );
		switch( eventFired )
		{
		case 0: //buffer ended event for source voice
			//reset the event manually; why Manually? well why not?!
			ResetEvent( hEvents[0] );

			//make sure there's a full number of buffers
			source->GetState( &voiceState );
			while( voiceState.BuffersQueued < STREAMINGWAVE_BUFFER_COUNT - 1 && !somethingsWrong )
			{
				//read and fill the next buffer to present
				switch( inFile.prepare() )
				{
				case StreamingWave::PR_EOF:
					//if end-of-file (or end-of-data), loop the file read
					inFile.resetFile(); //intentionally fall-through to loop sound

					//todo: don't loop!

				case StreamingWave::PR_SUCCESS:
					//present the next available buffer
					inFile.swap();
					//submit another buffer
					source->SubmitSourceBuffer( inFile.buffer() );
					source->GetState( &voiceState );
					
					//ofLogError()<<"Error in file load "<<sc->szFile;
					break;
				case StreamingWave::PR_FAILURE:
					somethingsWrong = true;
					break;
				}
			}

			break;
		case 1: //abort event
			quitting = true;
			break;
		default: //something's wrong...
			quitting = true;
		}
	}

	ofLogError()<<"Stopping and destroying?";

	//stop and destroy the voice
	source->Stop();
	source->FlushSourceBuffers();
	source->DestroyVoice();

	//close the streaming wave file;
	//this is done automatically in the class destructor,
	//so this is redundant
	inFile.close();

	//cleanup
	CoUninitialize();
	return 0;
}

bool initializeXAudioContext(){
	//required by XAudio2
	CoInitializeEx( NULL, COINIT_MULTITHREADED );

	//create the engine
	if( FAILED( XAudio2Create( &g_engine ) ) )
	{
		CoUninitialize();
		return false;
	}
	return true;
}

void closeXAudioContext(){
	//release the engine, cleanup
	g_engine->Release();
	CoUninitialize();
}

ofXAudioSoundPlayer::~ofXAudioSoundPlayer(){
	//signal all threads to end
	SetEvent( g_hAbortEvent );

	//wait for that thread to end
	WaitForSingleObject( hStreamingVoiceThread, INFINITE );

	//close all handles we opened
	CloseHandle( hStreamingVoiceThread );
	CloseHandle( g_hAbortEvent );
	closeXAudioContext();
}

bool ofXAudioSoundPlayer::loadSound(string fileName, bool stream){
	unloadSound();
	ofLogWarning()<<"LOADING "<<fileName<<endl;
	static bool bContextInit = false;
	if (!bContextInit ){
		bContextInit = initializeXAudioContext();
	}

	if (!bContextInit){
		ofLogError()<<"Error init XAudio2 context!";
		return false;
	}

	//create the mastering voice
	if( FAILED( g_engine->CreateMasteringVoice( &g_master ) ) )
	{
		ofLogError()<<"Error creating XAudio2 masering voice!";
		g_engine->Release();
		CoUninitialize();
		return false;
	} else {
		ofLogWarning()<<"Created mastering voice";
	}

	// right now, this only streams, doesn't load

	//prepare the context to send to the new thread
	streamContext.pVoice = NULL;
	streamContext.szFile = TEXT("F:/0280.wav");//LPCTSTR( fileName.c_str() );
	streamContext.hVoiceLoadEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

	//create the abort event to end all threads
	g_hAbortEvent = CreateEventW( NULL, TRUE, FALSE, NULL );

	//create the streaming voice thread
	DWORD dwThreadId = 0;

	// to do: OF thread, man!
	hStreamingVoiceThread = CreateThread( NULL, 0, StreamProc, &streamContext, 0, &dwThreadId );

	if( hStreamingVoiceThread == NULL )
	{
		ofLogError()<<"Error creating streaming voice thread!";
		CloseHandle( g_hAbortEvent );
		closeXAudioContext(); // should we really do this each time?
		return false;
	}

	//main loop
	startThread();

	return true;
};

void ofXAudioSoundPlayer::unloadSound(){
	if ( streamContext.pVoice != NULL (*streamContext.pVoice) != NULL){
		
		SetEvent( g_hAbortEvent );
		(*streamContext.pVoice)->Stop();
		(*streamContext.pVoice)->DestroyVoice();
	}
};

void ofXAudioSoundPlayer::play(){};
void ofXAudioSoundPlayer::stop(){};
	
void ofXAudioSoundPlayer::setVolume(float vol){};
void ofXAudioSoundPlayer::setPan(float vol){}; // -1 = left, 1 = right
void ofXAudioSoundPlayer::setSpeed(float spd){};
void ofXAudioSoundPlayer::setPaused(bool bP){};
void ofXAudioSoundPlayer::setLoop(bool bLp){};
void ofXAudioSoundPlayer::setMultiPlay(bool bMp){};
void ofXAudioSoundPlayer::setPosition(float pct){}; // 0 = start, 1 = end{};
void ofXAudioSoundPlayer::setPositionMS(int ms){};

float ofXAudioSoundPlayer::getPosition(){
	return 0.;
};
int ofXAudioSoundPlayer::getPositionMS(){
	return 0.;
};
bool ofXAudioSoundPlayer::getIsPlaying(){
	return true;
};

float ofXAudioSoundPlayer::getSpeed(){
	return 1.0;
};
float ofXAudioSoundPlayer::getPan(){
	return 0.;
};
bool ofXAudioSoundPlayer::isLoaded(){
	return true;
};
float ofXAudioSoundPlayer::getVolume(){
	return 1.0;
};

void ofXAudioSoundPlayer::threadedFunction(){
	ofLogWarning()<<"Waiting for load event";
	//while(isThreadRunning()){
		//wait for the streaming voice thread to signal that it's either
		//prepared the streaming, or that it's encountered an error
		WaitForSingleObject( streamContext.hVoiceLoadEvent, INFINITE );
		if( streamContext.pVoice == NULL || (*streamContext.pVoice) == NULL )
		{
			ofLogError()<<"Error loading stream!";
			SetEvent( g_hAbortEvent );
			WaitForSingleObject( hStreamingVoiceThread, INFINITE );

			CloseHandle( hStreamingVoiceThread );
			CloseHandle( g_hAbortEvent );
			closeXAudioContext();
			stopThread();
			return;
		}
		
		ofLogWarning()<<"All good, starting stream!";

		//start the streaming voice, which was created on the other thread
		if( streamContext.pVoice != NULL )
			(*streamContext.pVoice)->Start();
		else 
			ofLogError()<<"Why are we getting here? Stream still null?";
	//}
}



