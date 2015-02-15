//streamingWave.h
//by Jay Tennant 3/10/12
//loads and streams an unbuffered wave file
//win32developer.com
//this code provided free, as in public domain; score!

#ifndef STREAMINGWAVE_H
#define STREAMINGWAVE_H

#include <windows.h>
#include <mmiscapi.h>
#include <xaudio2.h>

class WaveInfo
{
private:
	WAVEFORMATEXTENSIBLE m_wf;
	DWORD m_dataOffset;
	DWORD m_dataLength;

protected:
	//looks for the FOURCC chunk, returning -1 on failure
	DWORD findChunk( HANDLE hFile, FOURCC cc, BYTE* memBuffer, DWORD sectorAlignment ) {
		DWORD dwChunkId = 0;
		DWORD dwChunkSize = 0;
		DWORD i = 0; //guaranteed to be always aligned with the sectors, except when done searching
		OVERLAPPED overlapped = {0};
		DWORD sectorOffset = 0;
		DWORD bytesRead = 0;

		bool searching = true;
		while( searching )
		{
			sectorOffset = 0;
			overlapped.Offset = i;
			if( FALSE == ReadFile( hFile, memBuffer, sectorAlignment, &bytesRead, &overlapped ) )
			{
				return -1;
			}

			bool needAnotherRead = false;
			while( searching && !needAnotherRead )
			{
				if( 8 + sectorOffset > sectorAlignment ) //reached the end of our memory buffer
				{
					needAnotherRead = true;
				}
				else if( 8 + sectorOffset > bytesRead ) //reached EOF, and not found a match
				{
					return -1;
				}
				else //looking through the read memory
				{
					dwChunkId = *reinterpret_cast<DWORD*>( memBuffer + sectorOffset );
					dwChunkSize = *reinterpret_cast<DWORD*>( memBuffer + sectorOffset + 4 );

					if( dwChunkId == cc ) //found a match
					{
						searching = false;
						i += sectorOffset;
					}
					else //no match found, add to offset
					{
						dwChunkSize += 8; //add offsets of the chunk id, and chunk size data entries
						dwChunkSize += 1;
						dwChunkSize &= 0xfffffffe; //guarantees WORD padding alignment

						if( i == 0 && sectorOffset == 0 ) //just in case we're at the 'RIFF' chunk; the dwChunkSize here means the entire file size
							sectorOffset += 12;
						else
							sectorOffset += dwChunkSize;
					}
				}
			}

			//if still searching, search the next sector
			if( searching )
			{
				i += sectorAlignment;
			}
		}

		return i;
	}

	//reads a certain amount of data in, returning the number of bytes copied
	DWORD readData( HANDLE hFile, DWORD bytesToRead, DWORD fileOffset, void* pDest, BYTE* memBuffer, DWORD sectorAlignment ) {
		if( bytesToRead == 0 )
			return 0;

		DWORD totalAmountCopied = 0;
		DWORD copyBeginOffset = fileOffset % sectorAlignment;
		OVERLAPPED overlapped = {0};
		bool fetchingData = true;
		DWORD pass = 0;
		DWORD dwNumberBytesRead = 0;

		//while fetching data
		while( fetchingData )
		{
			//calculate the sector to read
			overlapped.Offset = fileOffset - (fileOffset % sectorAlignment) + pass * sectorAlignment;

			//read the amount in; if the read failed, return 0
			if( FALSE == ReadFile( hFile, memBuffer, sectorAlignment, &dwNumberBytesRead, &overlapped ) )
				return 0;

			//if the full buffer was not filled (ie. EOF)
			if( dwNumberBytesRead < sectorAlignment )
			{
				//calculate how much can be copied
				DWORD amountToCopy = 0;
				if( dwNumberBytesRead > copyBeginOffset )
					amountToCopy = dwNumberBytesRead - copyBeginOffset;
				if( totalAmountCopied + amountToCopy > bytesToRead )
					amountToCopy = bytesToRead - totalAmountCopied;

				//copy that amount over
				memcpy( ((BYTE*)pDest) + totalAmountCopied, memBuffer + copyBeginOffset, amountToCopy );

				//add to the total amount copied
				totalAmountCopied += amountToCopy;

				//end the fetching data loop
				fetchingData = false;
			}
			//else
			else
			{
				//calculate how much can be copied
				DWORD amountToCopy = sectorAlignment - copyBeginOffset;
				if( totalAmountCopied + amountToCopy > bytesToRead )
					amountToCopy = bytesToRead - totalAmountCopied;

				//copy that amount over
				memcpy( ((BYTE*)pDest) + totalAmountCopied, memBuffer + copyBeginOffset, amountToCopy );

				//add to the total amount copied
				totalAmountCopied += amountToCopy;

				//set the copyBeginOffset to 0
				copyBeginOffset = 0;
			}

			//if the total amount equals the bytesToRead, end the fetching data loop
			if( totalAmountCopied == bytesToRead )
				fetchingData = false;

			//increment the pass
			pass++;
		}

		//return the total amount copied
		return totalAmountCopied;
	}

public:
	WaveInfo( LPCTSTR szFile = NULL ) : m_dataOffset(0), m_dataLength(0) {
		memset( &m_wf, 0, sizeof(m_wf) );
		load( szFile );
	}
	WaveInfo( const WaveInfo& c ) : m_wf(c.m_wf), m_dataOffset(c.m_dataOffset), m_dataLength(c.m_dataLength) {}

	//loads the wave format, offset to the wave data, and length of the wave data;
	//returns true on success, false on failure
	bool load( LPCTSTR szFile ) {
		memset( &m_wf, 0, sizeof(m_wf) );
		m_dataOffset = 0;
		m_dataLength = 0;

		if( szFile == NULL )
			return false;

		cout << "LOADING FILE" << endl;

		//load the file without system cacheing
		HANDLE hFile = CreateFileW( szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL );

		if( hFile == INVALID_HANDLE_VALUE )
			return false;

		
		cout << "SECTOR SIZE" << endl;

		//figure the sector size for reading
		DWORD dwSectorSize = 0;
		{
			DWORD dw1, dw2, dw3;
			GetDiskFreeSpace( NULL, &dw1, &dwSectorSize, &dw2, &dw3 );
		}

		//allocate the aligned memory buffer, used in finding and reading the chunks in the file
		BYTE *memBuffer = (BYTE*)_aligned_malloc( dwSectorSize, dwSectorSize );
		if( memBuffer == NULL )
		{
			CloseHandle( hFile );
			return false;
		}

		//look for 'RIFF' chunk
		DWORD dwChunkOffset = findChunk( hFile, MAKEFOURCC( 'R', 'I', 'F', 'F' ), memBuffer, dwSectorSize );
		if(dwChunkOffset == -1)
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}

		DWORD riffFormat = 0;
		//inFile.seekg( dwChunkOffset + 8, std::ios::beg );
		//inFile.read( reinterpret_cast<char*>(&riffFormat), sizeof(riffFormat) );
		if( sizeof(DWORD) != readData( hFile, sizeof(riffFormat), dwChunkOffset + 8, &riffFormat, memBuffer, dwSectorSize ) )
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}
		if(riffFormat != MAKEFOURCC('W', 'A', 'V', 'E'))
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}

		//look for 'fmt ' chunk
		dwChunkOffset = findChunk( hFile, MAKEFOURCC( 'f', 'm', 't', ' ' ), memBuffer, dwSectorSize );
		if( dwChunkOffset == -1 )
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}

		//read in first the WAVEFORMATEX structure
		//inFile.seekg( dwChunkOffset + 8, std::ios::beg );
		//inFile.read( reinterpret_cast<char*>(&m_wf.Format), sizeof(m_wf.Format) );
		if( sizeof(m_wf.Format) != readData( hFile, sizeof(m_wf.Format), dwChunkOffset + 8, &m_wf.Format, memBuffer, dwSectorSize ) )
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}
		if( m_wf.Format.cbSize == (sizeof(m_wf) - sizeof(m_wf.Format)) )
		{
			//read in whole WAVEFORMATEXTENSIBLE structure
			//inFile.seekg( dwChunkOffset + 8, std::ios::beg );
			//inFile.read( reinterpret_cast<char*>(&m_wf), sizeof(m_wf) );
			if( sizeof(m_wf) != readData( hFile, sizeof(m_wf), dwChunkOffset + 8, &m_wf, memBuffer, dwSectorSize ) )
			{
				_aligned_free( memBuffer );
				CloseHandle( hFile );
				return false;
			}
		}

		//look for 'data' chunk
		dwChunkOffset = findChunk( hFile, MAKEFOURCC( 'd', 'a', 't', 'a' ), memBuffer, dwSectorSize );
		if(dwChunkOffset == -1)
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}

		//set the offset to the wave data, read in length, then return
		m_dataOffset = dwChunkOffset + 8;
		//inFile.seekg( dwChunkOffset + 4, std::ios::beg );
		//inFile.read( reinterpret_cast<char*>(&m_dataLength), 4 );
		if( sizeof(m_dataLength) != readData( hFile, sizeof(m_dataLength), dwChunkOffset + 4, &m_dataLength, memBuffer, dwSectorSize ) )
		{
			_aligned_free( memBuffer );
			CloseHandle( hFile );
			return false;
		}

		_aligned_free( memBuffer );

		CloseHandle( hFile );

		return true;
	}

	//returns true if the format is WAVEFORMATEXTENSIBLE; false if WAVEFORMATEX
	bool isExtensible() const { return (m_wf.Format.cbSize > 0); }
	//retrieves the WAVEFORMATEX structure
	const WAVEFORMATEX* wf() const { return &m_wf.Format; }
	//retrieves the WAVEFORMATEXTENSIBLE structure; meaningless if the wave is not WAVEFORMATEXTENSIBLE
	const WAVEFORMATEXTENSIBLE* wfex() const { return &m_wf; }
	//gets the offset from the beginning of the file to the actual wave data
	DWORD getDataOffset() const { return m_dataOffset; }
	//gets the length of the wave data
	DWORD getDataLength() const { return m_dataLength; }
};


//should remain a power of 2; should also stay 4096 or larger, just to guarantee a multiple of the disk sector size (most are at or below 4096)
#define STREAMINGWAVE_BUFFER_SIZE 65536
//should never be less than 3
#define STREAMINGWAVE_BUFFER_COUNT 3

class StreamingWave : public WaveInfo
{
private:
	HANDLE m_hFile; //the file being streamed
	DWORD m_currentReadPass; //the current pass for reading; this number multiplied by STREAMINGWAVE_BUFFER_SIZE, adding getDataOffset(), represents the file position
	DWORD m_currentReadBuffer; //the current buffer used for reading from file; the presentation buffer is the one right before this
	bool m_isPrepared; //whether the buffer is prepared for the swap
	BYTE *m_dataBuffer; //the wave buffers; the size is STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment
	XAUDIO2_BUFFER m_xaBuffer[STREAMINGWAVE_BUFFER_COUNT]; //the xaudio2 buffer information
	DWORD m_sectorAlignment; //the sector alignment for reading; this value is added to the entire buffer's size for sector-aligned reading and reference
	DWORD m_bufferBeginOffset; //the starting offset for each buffer (when the file reads are offset by an amount)
public:
	StreamingWave( LPCTSTR szFile = NULL ) : WaveInfo( NULL ), m_hFile(INVALID_HANDLE_VALUE), m_currentReadPass(0), m_currentReadBuffer(0), m_isPrepared(false), 
		m_dataBuffer(NULL), m_sectorAlignment(0), m_bufferBeginOffset(0) {
			memset( m_xaBuffer, 0, sizeof(m_xaBuffer) );

			//figure the sector alignment
			DWORD dw1, dw2, dw3;
			GetDiskFreeSpace( NULL, &dw1, &m_sectorAlignment, &dw2, &dw3 );

			//allocate the buffers
			m_dataBuffer = (BYTE*)_aligned_malloc( STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment, m_sectorAlignment );
			memset( m_dataBuffer, 0, STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment );

			load( szFile );
	}
	StreamingWave( const StreamingWave& c ) : WaveInfo(c), m_hFile(c.m_hFile), m_currentReadPass(c.m_currentReadPass), m_currentReadBuffer(c.m_currentReadBuffer),
		m_isPrepared(c.m_isPrepared), m_dataBuffer(NULL), m_sectorAlignment(c.m_sectorAlignment), m_bufferBeginOffset(c.m_bufferBeginOffset) {
			if( m_sectorAlignment == 0 )
			{
				//figure the sector alignment
				DWORD dw1, dw2, dw3;
				GetDiskFreeSpace( NULL, &dw1, &m_sectorAlignment, &dw2, &dw3 );
			}

			//allocate the buffers
			m_dataBuffer = (BYTE*)_aligned_malloc( STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment, m_sectorAlignment );
			memset( m_dataBuffer, 0, STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment );

			memcpy( m_dataBuffer, c.m_dataBuffer, STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment );
			memcpy( m_xaBuffer, c.m_xaBuffer, sizeof(m_xaBuffer) );
			for( int i = 0; i < STREAMINGWAVE_BUFFER_COUNT; i++ )
				m_xaBuffer[i].pAudioData = m_dataBuffer + m_bufferBeginOffset + i * STREAMINGWAVE_BUFFER_SIZE;
	}
	~StreamingWave() {
		close();

		if( m_dataBuffer != NULL )
			_aligned_free( m_dataBuffer );
		m_dataBuffer = NULL;
	}

	//loads the file for streaming wave data
	bool load( LPCTSTR szFile ) {
		close();

		//test if the data can be loaded
		if( !WaveInfo::load( szFile ) )
			return false;

		//figure the offset for the wave data in allocated memory
		m_bufferBeginOffset = getDataOffset() % m_sectorAlignment;

		//open the file
		m_hFile = CreateFileW( szFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL );
		if( m_hFile == INVALID_HANDLE_VALUE )
			return false;

		//set the xaudio2 buffer struct to refer to appropriate buffer starting points (but leave size of the data as 0)
		for( int i = 0; i < STREAMINGWAVE_BUFFER_COUNT; i++ )
			m_xaBuffer[i].pAudioData = m_dataBuffer + m_bufferBeginOffset + i * STREAMINGWAVE_BUFFER_SIZE;

		return true;
	}

	//closes the file stream, resetting this object's state
	void close() {
		if( m_hFile != INVALID_HANDLE_VALUE )
			CloseHandle( m_hFile );
		m_hFile = INVALID_HANDLE_VALUE;

		m_bufferBeginOffset = 0;
		memset( m_xaBuffer, 0, sizeof(m_xaBuffer) );
		memset( m_dataBuffer, 0, STREAMINGWAVE_BUFFER_COUNT * STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment );
		m_isPrepared = false;
		m_currentReadBuffer = 0;
		m_currentReadPass = 0;

		WaveInfo::load( NULL );
	}

	//swaps the presentation buffer to the next one
	void swap() {m_currentReadBuffer = (m_currentReadBuffer + 1) % STREAMINGWAVE_BUFFER_COUNT; m_isPrepared = false;}

	//gets the current buffer
	const XAUDIO2_BUFFER* buffer() const {return &m_xaBuffer[ (m_currentReadBuffer + STREAMINGWAVE_BUFFER_COUNT - 1) % STREAMINGWAVE_BUFFER_COUNT ];}

	//resets the file pointer to the beginning of the wave data;
	//this will not wipe out buffers that have been prepared, so it is safe to call
	//after a call to prepare() has returned PR_EOF, and before a call to swap() has
	//been made to present the prepared buffer
	void resetFile() {m_currentReadPass = 0;}

	enum PREPARE_RESULT {
		PR_SUCCESS = 0,
		PR_FAILURE = 1,
		PR_EOF = 2,
	};

	//prepares the next buffer for presentation;
	//returns PR_SUCCESS on success,
	//PR_FAILURE on failure,
	//and PR_EOF when the end of the data has been reached
	DWORD prepare() {
		//validation check
		if( m_hFile == INVALID_HANDLE_VALUE )
		{
			m_xaBuffer[ m_currentReadBuffer ].AudioBytes = 0;
			m_xaBuffer[ m_currentReadBuffer ].Flags = XAUDIO2_END_OF_STREAM;
			return PR_FAILURE;
		}

		//are we already prepared?
		if( m_isPrepared )
			return PR_SUCCESS;

		//figure the offset of the file pointer
		OVERLAPPED overlapped = {0};
		overlapped.Offset = getDataOffset() - m_bufferBeginOffset + STREAMINGWAVE_BUFFER_SIZE * m_currentReadPass;

		//preliminary end-of-data check
		if( overlapped.Offset + m_bufferBeginOffset > getDataLength() + getDataOffset() )
		{
			m_xaBuffer[ m_currentReadBuffer ].AudioBytes = 0;
			m_xaBuffer[ m_currentReadBuffer ].Flags = XAUDIO2_END_OF_STREAM;
			m_isPrepared = true;
			return PR_EOF;
		}

		//read in data from file
		DWORD dwNumBytesRead = 0;
		if( FALSE == ReadFile( m_hFile, m_dataBuffer + STREAMINGWAVE_BUFFER_SIZE * m_currentReadBuffer, STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment, &dwNumBytesRead, &overlapped ) )
		{
			m_xaBuffer[ m_currentReadBuffer ].AudioBytes = 0;
			m_xaBuffer[ m_currentReadBuffer ].Flags = XAUDIO2_END_OF_STREAM;
			return PR_FAILURE;
		}

		//force dwNumBytesRead to be less than the actual amount read if reading past the end of the data chunk
		if( dwNumBytesRead + STREAMINGWAVE_BUFFER_SIZE * m_currentReadPass > getDataLength() )
		{
			if( STREAMINGWAVE_BUFFER_SIZE * m_currentReadPass <= getDataLength() )
				dwNumBytesRead = min( dwNumBytesRead, getDataLength() - STREAMINGWAVE_BUFFER_SIZE * m_currentReadPass ); //bytes read are from overlapping file chunks
			else
				dwNumBytesRead = 0; //none of the bytes are from the correct data chunk; this should never happen due to the preliminary end-of-data check, unless the file was wrong
		}

		//end-of-file/data check
		if( dwNumBytesRead < STREAMINGWAVE_BUFFER_SIZE + m_sectorAlignment )
		{
			//check for case where less than the sectorAlignment amount of data is still available in the file;
			//of course, only do something if there isn't that amount of data left
			if( dwNumBytesRead < m_bufferBeginOffset )
			{//no valid data at all; this shouldn't happen since the preliminary end-of-data check happened already, unless the file was wrong
				m_xaBuffer[ m_currentReadBuffer ].AudioBytes = 0;
				m_xaBuffer[ m_currentReadBuffer ].Flags = XAUDIO2_END_OF_STREAM;
				m_isPrepared = true;

				//increment the current read pass
				m_currentReadPass++;
				return PR_EOF;
			}
			else if( dwNumBytesRead - m_bufferBeginOffset <= STREAMINGWAVE_BUFFER_SIZE )
			{//some valid data; this should always happen for the end-of-file and end-of-data conditions
				m_xaBuffer[ m_currentReadBuffer ].AudioBytes = dwNumBytesRead - m_bufferBeginOffset; //do not include the data offset as valid data
				m_xaBuffer[ m_currentReadBuffer ].Flags = XAUDIO2_END_OF_STREAM;
				m_isPrepared = true;

				//increment the current read pass
				m_currentReadPass++;
				return PR_EOF;
			}
		}

		//set the amount of data available;
		//this should always be STREAMINGWAVE_BUFFER_SIZE, unless one of the previous conditions (end-of-file, end-of-data) were met
		m_xaBuffer[ m_currentReadBuffer ].AudioBytes = STREAMINGWAVE_BUFFER_SIZE;
		m_xaBuffer[ m_currentReadBuffer ].Flags = 0;
		m_isPrepared = true;

		//increment the current read pass
		m_currentReadPass++;

		//return success
		return PR_SUCCESS;
	}
};

#endif