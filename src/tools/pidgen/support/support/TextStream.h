/*
 * Copyright (c) 2005 Palmsource, Inc.
 * 
 * This software is licensed as described in the file LICENSE, which
 * you should have received as part of this distribution. The terms
 * are also available at http://www.openbinder.org/license.html.
 * 
 * This software consists of voluntary contributions made by many
 * individuals. For the exact contribution history, see the revision
 * history and logs, available at http://www.openbinder.org
 */

#ifndef	_SUPPORT_TEXTSTREAM_H
#define	_SUPPORT_TEXTSTREAM_H

/*!	@file support/TextStream.h
	@ingroup CoreSupportDataModel
	@brief Implementation of ITextOutput and ITextInput on top of byte streams.
*/

#include <support/Atom.h>
#include <support/ITextStream.h>
//#include <support/ByteStream.h>
//#include <support/Locker.h>
#include <support/VectorIO.h>

#if _SUPPORTS_NAMESPACE
namespace palmos {
namespace support {
#endif

/*!	@addtogroup CoreSupportDataModel
	@{
*/

//! Flags for BTextOutput constructor.
enum {
	B_TEXT_OUTPUT_THREADED			= 0x00000001,	//!< Buffer text per-thread.
	B_TEXT_OUTPUT_COLORED			= 0x00000002,	//!< Lines are colored by thread.
	B_TEXT_OUTPUT_TAG_THREAD		= 0x00000004,	//!< Prefix output with thread id.
	B_TEXT_OUTPUT_TAG_TEAM			= 0x00000008,	//!< Prefix output with team id.
	B_TEXT_OUTPUT_TAG_TIME			= 0x00000010,	//!< Prefix output with timestamp.
	B_TEXT_OUTPUT_FROM_ENV			= 0x10000000,	//!< Get above flags from environment var.

	B_TEXT_OUTPUT_COLORED_RED		= 0x00010000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_GREEN		= 0x00020000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_BROWN		= 0x00030000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_BLUE		= 0x00040000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_PURPLE	= 0x00050000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_CYAN		= 0x00060000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_GRAY		= 0x00070000 | B_TEXT_OUTPUT_COLORED,
	B_TEXT_OUTPUT_COLORED_MASK		= 0x000f0000
};

#define TEXTOUTPUT_SMALL_STACK 1

/*-----------------------------------------------------------------*/

class BVFSFile : public SAtom
{
	public:
		enum flags
		{
			DELETE_IF_EMPTY		= 1
		};
		
		
							BVFSFile();
							BVFSFile(const char* path, int flags, int mode=0, status_t * outResult=0);

				status_t	InitCheck() const;
				
				// Set the object to access the given file.  Opens the file.
				status_t	SetTo(const char* path, int flags, int mode=0);

				bool		IsReadable() const;
				bool		IsWritable() const;
	
				uint16_t	VolRefNum() const;	

				status_t	Delete();
		
		virtual	off_t		Size() const;			// query for total file size
		virtual	status_t	SetSize(off_t size);	// resize file
		
		virtual	ssize_t		ReadAtV(off_t position, const struct iovec *vector, ssize_t count);
		virtual	ssize_t		WriteAtV(off_t position, const struct iovec *vector, ssize_t count);

		virtual	ssize_t		ReadV(const struct iovec *vector, ssize_t count);
		virtual	ssize_t		WriteV(const struct iovec *vector, ssize_t count);
		
		virtual	status_t	Sync();

		virtual				~BVFSFile();
		
private:
	status_t		m_initStatus;
	int				m_fd;
	int				m_flags;
	off_t			m_pos;
};


class BTextOutput : public ITextOutput
{
	public:

										BTextOutput(const sptr<BVFSFile>& stream, uint32_t flags = 0);
		virtual							~BTextOutput();

		virtual	status_t				Print(	const char *debugText,
												ssize_t len = -1);
		virtual void					MoveIndent(	int32 delta);
		
		virtual	status_t				LogV(	const log_info& info,
												const iovec *vector,
												ssize_t count,
												uint32_t flags = 0);

		virtual	void					Flush();
		virtual	status_t				Sync();

				class _IMPEXP_SUPPORT style_state
				{
				public:
					int32_t	tag;
					vint32	indent;
					int32_t	startIndent;
					int32_t	front;
					
					int32_t	buffering;
					char*	buffer;
					ssize_t	bufferLen;
					ssize_t	bufferAvail;
					
#if TEXTOUTPUT_SMALL_STACK
					// Formatting state to avoid using a lot of stack
					// space.  This is really problematic, because it
					// means this doesn't work at all if B_TEXT_OUTPUT_THREADED
					// is not enabled and multiple threads are using
					// the stream.  *sigh*
					log_info	tmp_log;
					SVectorIO	tmp_vecio;
					char		tmp_prefix[64];
#endif

							style_state();
							~style_state();
				};
//	protected:

										BTextOutput(BVFSFile *This, uint32_t flags = 0);
	
		// TO DO: Implement LTextOutput and RTextOutput.
//		virtual	sptr<IBinder>		AsBinderImpl();
//		virtual	sptr<const IBinder>	AsBinderImpl() const;
		
	private:
				struct thread_styles;
				
				void					InitStyles();
				const char*				MakeIndent(style_state *style, int32_t* out_indent);
				const char*				MakeIndent(int32_t* inout_indent);
				style_state *			Style();
		static	void					DeleteStyle(void *style);

				BVFSFile *				m_stream;
				uint32_t				m_flags;
				
				style_state				m_globalStyle;
				thread_styles*			m_threadStyles;
				int32_t					m_nextTag;
				
				enum {
					MAX_COLORS = 16
				};
				size_t					m_numColors;
				char					m_colors[MAX_COLORS];

				style_state				m_style;
private:
									BTextOutput(const BTextOutput& o);	// no implementation
		BTextOutput&				operator=(const BTextOutput& o);	// no implementation
};

/*-----------------------------------------------------------------*/
/*
class BTextInput : public ITextInput
{
	public:

										BTextInput(const sptr<IByteInput>& stream, uint32_t flags = 0);
		virtual							~BTextInput();

		virtual	ssize_t					ReadChar();
		virtual	ssize_t					ReadLineBuffer(char* buffer, size_t size);
		// AppendLineTo: reads a line from the stream and appends it to
		// outString.
		virtual	ssize_t					AppendLineTo(SString* outString);

	protected:

										BTextInput(IByteInput *This, uint32_t flags = 0);
	
		// TO DO: Implement LTextOutput and RTextOutput.
		virtual	sptr<IBinder>		AsBinderImpl();
		virtual	sptr<const IBinder>	AsBinderImpl() const;
		
	private:
				IByteInput *			m_stream;
				uint32_t				m_flags;
				
				SLocker					m_lock;
				char					m_buffer[128];
				ssize_t					m_pos;
				ssize_t					m_avail;
				bool					m_inCR;
};
*/
/*-----------------------------------------------------------------*/

/*!	@} */

#if _SUPPORTS_NAMESPACE
} } // namespace palmos::support
#endif

#endif /* _SUPPORT_TEXTSTREAM_H */
