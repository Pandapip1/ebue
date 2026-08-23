/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The native NT interface ntlibc is built on: types, structures, constants
 * and prototypes for the ntdll routines used anywhere in the library.  This
 * is deliberately self-contained -- no windows.h, no winternl.h -- so that
 * the library builds with nothing but its own headers and tcc.
 *
 * Layouts are the documented/observed ones for both i386 and x86_64, and
 * are expressed with pointer-sized fields so that one definition serves
 * both.  Where a structure is only ever read at a few offsets, only the
 * leading fields up to the last one needed are declared, and a comment
 * says so.
 */
#ifndef _NTLIBC_NT_H
#define _NTLIBC_NT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __i386__
#define NTAPI __attribute__((stdcall))
#else
#define NTAPI
#endif

#define TRUE 1
#define FALSE 0

typedef int NTSTATUS;
typedef void *HANDLE, **PHANDLE;
typedef void *PVOID;
typedef unsigned char BOOLEAN, UCHAR;
typedef unsigned short USHORT, WCHAR, *PWSTR;
typedef const WCHAR *PCWSTR;
typedef unsigned long ULONG, *PULONG;
typedef long LONG;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
typedef size_t SIZE_T;
typedef ULONG ACCESS_MASK;
typedef LONGLONG LARGE_INTEGER;
typedef ULONGLONG ULARGE_INTEGER;
typedef USHORT RTL_ATOM;

#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define NtCurrentThread()  ((HANDLE)(LONG_PTR)-2)

/* ---- status codes ---------------------------------------------------- */
#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
#define STATUS_WAIT_0                   ((NTSTATUS)0x00000000L)
#define STATUS_TIMEOUT                  ((NTSTATUS)0x00000102L)
#define STATUS_PENDING                  ((NTSTATUS)0x00000103L)
#define STATUS_PROCESS_CLONED           ((NTSTATUS)0x00000129L)
#define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x80000005L)
#define STATUS_NO_MORE_FILES            ((NTSTATUS)0x80000006L)
#define STATUS_DATATYPE_MISALIGNMENT    ((NTSTATUS)0x80000002L)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_INFO_CLASS       ((NTSTATUS)0xC0000003L)
#define STATUS_INFO_LENGTH_MISMATCH     ((NTSTATUS)0xC0000004L)
#define STATUS_ACCESS_VIOLATION         ((NTSTATUS)0xC0000005L)
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
#define STATUS_NO_SUCH_DEVICE           ((NTSTATUS)0xC000000EL)
#define STATUS_NO_SUCH_FILE             ((NTSTATUS)0xC000000FL)
#define STATUS_INVALID_DEVICE_REQUEST   ((NTSTATUS)0xC0000010L)
#define STATUS_END_OF_FILE              ((NTSTATUS)0xC0000011L)
#define STATUS_NO_MEMORY                ((NTSTATUS)0xC0000017L)
#define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_TYPE_MISMATCH     ((NTSTATUS)0xC0000024L)
#define STATUS_OBJECT_NAME_INVALID      ((NTSTATUS)0xC0000033L)
#define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION    ((NTSTATUS)0xC0000035L)
#define STATUS_OBJECT_PATH_INVALID      ((NTSTATUS)0xC0000039L)
#define STATUS_OBJECT_PATH_NOT_FOUND    ((NTSTATUS)0xC000003AL)
#define STATUS_OBJECT_PATH_SYNTAX_BAD   ((NTSTATUS)0xC000003BL)
#define STATUS_DATA_ERROR               ((NTSTATUS)0xC000003EL)
#define STATUS_SHARING_VIOLATION        ((NTSTATUS)0xC0000043L)
#define STATUS_DELETE_PENDING           ((NTSTATUS)0xC0000056L)
#define STATUS_DISK_FULL                ((NTSTATUS)0xC000007FL)
#define STATUS_TOO_MANY_OPENED_FILES    ((NTSTATUS)0xC000011FL)
#define STATUS_FILE_IS_A_DIRECTORY      ((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BBL)
#define STATUS_PIPE_BROKEN              ((NTSTATUS)0xC000014BL)
#define STATUS_PIPE_DISCONNECTED        ((NTSTATUS)0xC00000B0L)
#define STATUS_PIPE_EMPTY               ((NTSTATUS)0xC00000D9L)
#define STATUS_PIPE_LISTENING           ((NTSTATUS)0xC00000B3L)
#define STATUS_PIPE_CLOSING             ((NTSTATUS)0xC00000B1L)
#define STATUS_PIPE_NOT_AVAILABLE       ((NTSTATUS)0xC00000ACL)
#define STATUS_DIRECTORY_NOT_EMPTY      ((NTSTATUS)0xC0000101L)
#define STATUS_NOT_A_DIRECTORY          ((NTSTATUS)0xC0000103L)
#define STATUS_NAME_TOO_LONG            ((NTSTATUS)0xC0000106L)
#define STATUS_CANNOT_DELETE            ((NTSTATUS)0xC0000121L)
#define STATUS_FILE_DELETED             ((NTSTATUS)0xC0000123L)
#define STATUS_PROCESS_IS_TERMINATING   ((NTSTATUS)0xC000010AL)
#define STATUS_MEDIA_WRITE_PROTECTED    ((NTSTATUS)0xC00000A2L)
#define STATUS_INVALID_IMAGE_FORMAT     ((NTSTATUS)0xC000007BL)
#define STATUS_INVALID_IMAGE_NOT_MZ     ((NTSTATUS)0xC000012FL)
#define STATUS_INVALID_IMAGE_WIN_32     ((NTSTATUS)0xC0000359L)
#define STATUS_INVALID_IMAGE_WIN_64     ((NTSTATUS)0xC000035AL)
#define STATUS_NOT_SAME_DEVICE          ((NTSTATUS)0xC00000D4L)
#define STATUS_FILE_CLOSED              ((NTSTATUS)0xC0000128L)
#define STATUS_IO_TIMEOUT               ((NTSTATUS)0xC00000B5L)
#define STATUS_CANCELLED                ((NTSTATUS)0xC0000120L)
#define STATUS_QUOTA_EXCEEDED           ((NTSTATUS)0xC0000044L)
#define STATUS_IO_REPARSE_TAG_NOT_HANDLED ((NTSTATUS)0xC0000279L)
#define STATUS_DLL_NOT_FOUND            ((NTSTATUS)0xC0000135L)
#define STATUS_ENTRYPOINT_NOT_FOUND     ((NTSTATUS)0xC0000139L)
#define STATUS_FILE_INVALID             ((NTSTATUS)0xC0000098L)
#define STATUS_TOO_MANY_LINKS           ((NTSTATUS)0xC0000265L)
#define STATUS_NOT_A_REPARSE_POINT      ((NTSTATUS)0xC0000275L)
#define STATUS_PRIVILEGE_NOT_HELD       ((NTSTATUS)0xC0000061L)
#define STATUS_USER_MAPPED_FILE         ((NTSTATUS)0xC0000243L)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009AL)
#define STATUS_DEVICE_NOT_READY         ((NTSTATUS)0xC00000A3L)
#define STATUS_FILE_TOO_LARGE           ((NTSTATUS)0xC0000904L)
#define STATUS_VOLUME_DISMOUNTED        ((NTSTATUS)0xC000026EL)
#define STATUS_NOT_FOUND                ((NTSTATUS)0xC0000225L)
#define STATUS_CONTROL_C_EXIT           ((NTSTATUS)0xC000013AL)

/* ---- basic structures ------------------------------------------------ */
typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _STRING {
	USHORT Length;
	USHORT MaximumLength;
	char  *Buffer;
} STRING, ANSI_STRING, *PSTRING;

typedef struct _OBJECT_ATTRIBUTES {
	ULONG Length;
	HANDLE RootDirectory;
	PUNICODE_STRING ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor;
	PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define OBJ_INHERIT             0x00000002L
#define OBJ_PERMANENT           0x00000010L
#define OBJ_EXCLUSIVE           0x00000020L
#define OBJ_CASE_INSENSITIVE    0x00000040L
#define OBJ_OPENIF              0x00000080L
#define OBJ_OPENLINK            0x00000100L

#define InitializeObjectAttributes(p, n, a, r, s) do { \
	(p)->Length = sizeof(OBJECT_ATTRIBUTES); \
	(p)->RootDirectory = (r); \
	(p)->Attributes = (a); \
	(p)->ObjectName = (n); \
	(p)->SecurityDescriptor = (s); \
	(p)->SecurityQualityOfService = NULL; \
} while (0)

typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		PVOID Pointer;
	};
	ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _CLIENT_ID {
	HANDLE UniqueProcess;
	HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _LIST_ENTRY {
	struct _LIST_ENTRY *Flink;
	struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _CURDIR {
	UNICODE_STRING DosPath;
	HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR {
	USHORT Flags;
	USHORT Length;
	ULONG TimeStamp;
	STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS {
	ULONG MaximumLength;
	ULONG Length;
	ULONG Flags;
	ULONG DebugFlags;
	HANDLE ConsoleHandle;
	ULONG ConsoleFlags;
	HANDLE StandardInput;
	HANDLE StandardOutput;
	HANDLE StandardError;
	CURDIR CurrentDirectory;
	UNICODE_STRING DllPath;
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
	PVOID Environment;
	ULONG StartingX;
	ULONG StartingY;
	ULONG CountX;
	ULONG CountY;
	ULONG CountCharsX;
	ULONG CountCharsY;
	ULONG FillAttribute;
	ULONG WindowFlags;
	ULONG ShowWindowFlags;
	UNICODE_STRING WindowTitle;
	UNICODE_STRING DesktopInfo;
	UNICODE_STRING ShellInfo;
	UNICODE_STRING RuntimeData;
	RTL_DRIVE_LETTER_CURDIR CurrentDirectories[32];
	ULONG_PTR EnvironmentSize;
	ULONG_PTR EnvironmentVersion;
	PVOID PackageDependencyData;
	ULONG ProcessGroupId;
	ULONG LoaderThreads;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

#define RTL_USER_PROC_PARAMS_NORMALIZED 0x00000001
#define STARTF_USESTDHANDLES            0x00000100

typedef struct _PEB_LDR_DATA {
	ULONG Length;
	BOOLEAN Initialized;
	HANDLE SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
	PVOID EntryInProgress;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	/* more follows; not needed */
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

/* The PEB, up to ProcessHeap; the rest is not used here.  Padding after
 * the four leading bytes falls out of natural alignment on both arches. */
typedef struct _PEB {
	BOOLEAN InheritedAddressSpace;
	BOOLEAN ReadImageFileExecOptions;
	BOOLEAN BeingDebugged;
	BOOLEAN BitField;
	HANDLE Mutant;
	PVOID ImageBaseAddress;
	PPEB_LDR_DATA Ldr;
	PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
	PVOID SubSystemData;
	PVOID ProcessHeap;
	PVOID FastPebLock;
	PVOID AtlThunkSListPtr;
	PVOID IFEOKey;
	ULONG CrossProcessFlags;
	PVOID KernelCallbackTable;
	ULONG SystemReserved;
	ULONG AtlThunkSListPtr32;
	PVOID ApiSetMap;
	ULONG TlsExpansionCounter;
	PVOID TlsBitmap;
	ULONG TlsBitmapBits[2];
	PVOID ReadOnlySharedMemoryBase;
	PVOID SharedData;
	PVOID *ReadOnlyStaticServerData;
	PVOID AnsiCodePageData;
	PVOID OemCodePageData;
	PVOID UnicodeCaseTableData;
	ULONG NumberOfProcessors;
	ULONG NtGlobalFlag;
	LARGE_INTEGER CriticalSectionTimeout;
	SIZE_T HeapSegmentReserve;
	SIZE_T HeapSegmentCommit;
	SIZE_T HeapDeCommitTotalFreeThreshold;
	SIZE_T HeapDeCommitFreeBlockThreshold;
	ULONG NumberOfHeaps;
	ULONG MaximumNumberOfHeaps;
	PVOID *ProcessHeaps;
	PVOID GdiSharedHandleTable;
	PVOID ProcessStarterHelper;
	ULONG GdiDCAttributeList;
	PVOID LoaderLock;
	ULONG OSMajorVersion;
	ULONG OSMinorVersion;
	USHORT OSBuildNumber;
	USHORT OSCSDVersion;
	ULONG OSPlatformId;
	ULONG ImageSubsystem;
	ULONG ImageSubsystemMajorVersion;
	ULONG ImageSubsystemMinorVersion;
} PEB, *PPEB;

typedef struct _NT_TIB {
	PVOID ExceptionList;
	PVOID StackBase;
	PVOID StackLimit;
	PVOID SubSystemTib;
	PVOID FiberData;
	PVOID ArbitraryUserPointer;
	struct _NT_TIB *Self;
} NT_TIB;

typedef struct _TEB {
	NT_TIB NtTib;
	PVOID EnvironmentPointer;
	CLIENT_ID ClientId;
	PVOID ActiveRpcHandle;
	PVOID ThreadLocalStoragePointer;
	PPEB ProcessEnvironmentBlock;
	ULONG LastErrorValue;
	ULONG CountOfOwnedCriticalSections;
	PVOID CsrClientThread;
	PVOID Win32ThreadInfo;
	ULONG User32Reserved[26];
	ULONG UserReserved[5];
	PVOID WOW32Reserved;
	ULONG CurrentLocale;
	ULONG FpSoftwareStatusRegister;
	/* more follows; not needed */
} TEB, *PTEB;

/* ---- files ----------------------------------------------------------- */
#define FILE_READ_DATA            0x0001
#define FILE_LIST_DIRECTORY       0x0001
#define FILE_WRITE_DATA           0x0002
#define FILE_ADD_FILE             0x0002
#define FILE_APPEND_DATA          0x0004
#define FILE_ADD_SUBDIRECTORY     0x0004
#define FILE_READ_EA              0x0008
#define FILE_WRITE_EA             0x0010
#define FILE_EXECUTE              0x0020
#define FILE_TRAVERSE             0x0020
#define FILE_DELETE_CHILD         0x0040
#define FILE_READ_ATTRIBUTES      0x0080
#define FILE_WRITE_ATTRIBUTES     0x0100
#define DELETE                    0x00010000L
#define READ_CONTROL              0x00020000L
#define WRITE_DAC                 0x00040000L
#define WRITE_OWNER               0x00080000L
#define SYNCHRONIZE               0x00100000L
#define STANDARD_RIGHTS_REQUIRED  0x000F0000L
#define STANDARD_RIGHTS_READ      READ_CONTROL
#define STANDARD_RIGHTS_WRITE     READ_CONTROL
#define GENERIC_READ              0x80000000L
#define GENERIC_WRITE             0x40000000L
#define GENERIC_EXECUTE           0x20000000L
#define GENERIC_ALL               0x10000000L
#define FILE_GENERIC_READ  (STANDARD_RIGHTS_READ|FILE_READ_DATA|FILE_READ_ATTRIBUTES|FILE_READ_EA|SYNCHRONIZE)
#define FILE_GENERIC_WRITE (STANDARD_RIGHTS_WRITE|FILE_WRITE_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|FILE_APPEND_DATA|SYNCHRONIZE)
#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_READ|FILE_READ_ATTRIBUTES|FILE_EXECUTE|SYNCHRONIZE)

#define FILE_SHARE_READ           0x00000001
#define FILE_SHARE_WRITE          0x00000002
#define FILE_SHARE_DELETE         0x00000004
#define FILE_SHARE_VALID_FLAGS    0x00000007

#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
#define FILE_ATTRIBUTE_DEVICE               0x00000040
#define FILE_ATTRIBUTE_NORMAL               0x00000080
#define FILE_ATTRIBUTE_TEMPORARY            0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT        0x00000400
#define FILE_ATTRIBUTE_COMPRESSED           0x00000800
#define FILE_ATTRIBUTE_OFFLINE              0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000

#define FILE_SUPERSEDE                  0x00000000
#define FILE_OPEN                       0x00000001
#define FILE_CREATE                     0x00000002
#define FILE_OPEN_IF                    0x00000003
#define FILE_OVERWRITE                  0x00000004
#define FILE_OVERWRITE_IF               0x00000005

#define FILE_DIRECTORY_FILE             0x00000001
#define FILE_WRITE_THROUGH              0x00000002
#define FILE_SEQUENTIAL_ONLY            0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING  0x00000008
#define FILE_SYNCHRONOUS_IO_ALERT       0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT    0x00000020
#define FILE_NON_DIRECTORY_FILE         0x00000040
#define FILE_CREATE_TREE_CONNECTION     0x00000080
#define FILE_COMPLETE_IF_OPLOCKED       0x00000100
#define FILE_NO_EA_KNOWLEDGE            0x00000200
#define FILE_OPEN_REMOTE_INSTANCE       0x00000400
#define FILE_RANDOM_ACCESS              0x00000800
#define FILE_DELETE_ON_CLOSE            0x00001000
#define FILE_OPEN_BY_FILE_ID            0x00002000
#define FILE_OPEN_FOR_BACKUP_INTENT     0x00004000
#define FILE_NO_COMPRESSION             0x00008000
#define FILE_OPEN_REQUIRING_OPLOCK      0x00010000
#define FILE_RESERVE_OPFILTER           0x00100000
#define FILE_OPEN_REPARSE_POINT         0x00200000
#define FILE_OPEN_NO_RECALL             0x00400000
#define FILE_OPEN_FOR_FREE_SPACE_QUERY  0x00800000

#define FILE_SUPERSEDED                 0x00000000
#define FILE_OPENED                     0x00000001
#define FILE_CREATED                    0x00000002
#define FILE_OVERWRITTEN                0x00000003
#define FILE_EXISTS                     0x00000004
#define FILE_DOES_NOT_EXIST             0x00000005

#define FILE_PIPE_BYTE_STREAM_TYPE      0x00000000
#define FILE_PIPE_MESSAGE_TYPE          0x00000001
#define FILE_PIPE_BYTE_STREAM_MODE      0x00000000
#define FILE_PIPE_MESSAGE_MODE          0x00000001
#define FILE_PIPE_QUEUE_OPERATION       0x00000000
#define FILE_PIPE_COMPLETE_OPERATION    0x00000001

#define FILE_WRITE_TO_END_OF_FILE       (-1LL)
#define FILE_USE_FILE_POINTER_POSITION  (-2LL)

#define FILE_DEVICE_BEEP                0x00000001
#define FILE_DEVICE_CD_ROM              0x00000002
#define FILE_DEVICE_CONSOLE             0x00000050
#define FILE_DEVICE_DISK                0x00000007
#define FILE_DEVICE_DISK_FILE_SYSTEM    0x00000008
#define FILE_DEVICE_NAMED_PIPE          0x00000011
#define FILE_DEVICE_NETWORK_FILE_SYSTEM 0x00000014
#define FILE_DEVICE_NULL                0x00000015
#define FILE_DEVICE_SCREEN              0x0000001C
#define FILE_DEVICE_SERIAL_PORT         0x0000001B
#define FILE_DEVICE_UNKNOWN             0x00000022
#define FILE_DEVICE_VIRTUAL_DISK        0x00000024
#define FILE_DEVICE_MAILSLOT            0x0000000C
#define FILE_DEVICE_CD_ROM_FILE_SYSTEM  0x00000003
#define FILE_DEVICE_TAPE_FILE_SYSTEM    0x00000020
#define FILE_DEVICE_FILE_SYSTEM         0x00000009

#define FILE_REMOVABLE_MEDIA            0x00000001
#define FILE_READ_ONLY_DEVICE           0x00000002
#define FILE_REMOTE_DEVICE              0x00000010
#define FILE_DEVICE_IS_MOUNTED          0x00000020

typedef enum _FILE_INFORMATION_CLASS {
	FileDirectoryInformation = 1,
	FileFullDirectoryInformation,
	FileBothDirectoryInformation,
	FileBasicInformation,
	FileStandardInformation,
	FileInternalInformation,
	FileEaInformation,
	FileAccessInformation,
	FileNameInformation,
	FileRenameInformation,
	FileLinkInformation,
	FileNamesInformation,
	FileDispositionInformation,
	FilePositionInformation,
	FileFullEaInformation,
	FileModeInformation,
	FileAlignmentInformation,
	FileAllInformation,
	FileAllocationInformation,
	FileEndOfFileInformation,
	FileAlternateNameInformation,
	FileStreamInformation,
	FilePipeInformation,
	FilePipeLocalInformation,
	FilePipeRemoteInformation,
	FileMailslotQueryInformation,
	FileMailslotSetInformation,
	FileCompressionInformation,
	FileObjectIdInformation,
	FileCompletionInformation,
	FileMoveClusterInformation,
	FileQuotaInformation,
	FileReparsePointInformation,
	FileNetworkOpenInformation,
	FileAttributeTagInformation,
	FileTrackingInformation,
	FileIdBothDirectoryInformation,
	FileIdFullDirectoryInformation,
	FileValidDataLengthInformation,
	FileShortNameInformation,
	FileIoCompletionNotificationInformation,
	FileIoStatusBlockRangeInformation,
	FileIoPriorityHintInformation,
	FileSfioReserveInformation,
	FileSfioVolumeInformation,
	FileHardLinkInformation,
	FileProcessIdsUsingFileInformation,
	FileNormalizedNameInformation,
	FileNetworkPhysicalNameInformation,
	FileIdGlobalTxDirectoryInformation,
	FileIsRemoteDeviceInformation,
	FileUnusedInformation,
	FileNumaNodeInformation,
	FileStandardLinkInformation,
	FileRemoteProtocolInformation,
	FileRenameInformationBypassAccessCheck,
	FileLinkInformationBypassAccessCheck,
	FileVolumeNameInformation,
	FileIdInformation,
	FileIdExtdDirectoryInformation,
	FileReplaceCompletionInformation,
	FileHardLinkFullIdInformation,
	FileIdExtdBothDirectoryInformation,
	FileDispositionInformationEx,
	FileRenameInformationEx,
	FileRenameInformationExBypassAccessCheck,
	FileDesiredStorageClassInformation,
	FileStatInformation,
	FileMemoryPartitionInformation,
	FileStatLxInformation,
	FileCaseSensitiveInformation,
	FileLinkInformationEx,
	FileLinkInformationExBypassAccessCheck,
	FileStorageReserveIdInformation,
	FileCaseSensitiveInformationForceAccessCheck,
	FileMaximumInformation
} FILE_INFORMATION_CLASS;

typedef enum _FS_INFORMATION_CLASS {
	FileFsVolumeInformation = 1,
	FileFsLabelInformation,
	FileFsSizeInformation,
	FileFsDeviceInformation,
	FileFsAttributeInformation,
	FileFsControlInformation,
	FileFsFullSizeInformation,
	FileFsObjectIdInformation,
	FileFsDriverPathInformation,
	FileFsVolumeFlagsInformation,
	FileFsSectorSizeInformation,
	FileFsMaximumInformation
} FS_INFORMATION_CLASS;

typedef struct _FILE_BASIC_INFORMATION {
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	ULONG FileAttributes;
} FILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG NumberOfLinks;
	BOOLEAN DeletePending;
	BOOLEAN Directory;
} FILE_STANDARD_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION {
	LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
	LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
	LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_ALLOCATION_INFORMATION {
	LARGE_INTEGER AllocationSize;
} FILE_ALLOCATION_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
	BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION_EX {
	ULONG Flags;
} FILE_DISPOSITION_INFORMATION_EX;

#define FILE_DISPOSITION_DO_NOT_DELETE              0x00000000
#define FILE_DISPOSITION_DELETE                     0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS            0x00000002
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK  0x00000004
#define FILE_DISPOSITION_ON_CLOSE                   0x00000008
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE  0x00000010

typedef struct _FILE_RENAME_INFORMATION {
	union {
		BOOLEAN ReplaceIfExists;
		ULONG Flags;
	};
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_RENAME_INFORMATION;

#define FILE_RENAME_REPLACE_IF_EXISTS               0x00000001
#define FILE_RENAME_POSIX_SEMANTICS                 0x00000002

typedef struct _FILE_NAME_INFORMATION {
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_NAME_INFORMATION;

typedef struct _FILE_ATTRIBUTE_TAG_INFORMATION {
	ULONG FileAttributes;
	ULONG ReparseTag;
} FILE_ATTRIBUTE_TAG_INFORMATION;

typedef struct _FILE_NETWORK_OPEN_INFORMATION {
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION;

typedef struct _FILE_MODE_INFORMATION {
	ULONG Mode;
} FILE_MODE_INFORMATION;

typedef struct _FILE_ACCESS_INFORMATION {
	ACCESS_MASK AccessFlags;
} FILE_ACCESS_INFORMATION;

typedef struct _FILE_DIRECTORY_INFORMATION {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION;

typedef struct _FILE_ID_BOTH_DIR_INFORMATION {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	ULONG EaSize;
	char ShortNameLength;
	WCHAR ShortName[12];
	LARGE_INTEGER FileId;
	WCHAR FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION;

typedef struct _FILE_FS_DEVICE_INFORMATION {
	ULONG DeviceType;
	ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION;

typedef struct _FILE_FS_VOLUME_INFORMATION {
	LARGE_INTEGER VolumeCreationTime;
	ULONG VolumeSerialNumber;
	ULONG VolumeLabelLength;
	BOOLEAN SupportsObjects;
	WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION;

typedef struct _FILE_FS_SIZE_INFORMATION {
	LARGE_INTEGER TotalAllocationUnits;
	LARGE_INTEGER AvailableAllocationUnits;
	ULONG SectorsPerAllocationUnit;
	ULONG BytesPerSector;
} FILE_FS_SIZE_INFORMATION;

typedef struct _FILE_PIPE_LOCAL_INFORMATION {
	ULONG NamedPipeType;
	ULONG NamedPipeConfiguration;
	ULONG MaximumInstances;
	ULONG CurrentInstances;
	ULONG InboundQuota;
	ULONG ReadDataAvailable;
	ULONG OutboundQuota;
	ULONG WriteQuotaAvailable;
	ULONG NamedPipeState;
	ULONG NamedPipeEnd;
} FILE_PIPE_LOCAL_INFORMATION;

#define FILE_PIPE_CONNECTED_STATE 3

typedef struct _REPARSE_DATA_BUFFER {
	ULONG ReparseTag;
	USHORT ReparseDataLength;
	USHORT Reserved;
	union {
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			ULONG Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			UCHAR DataBuffer[1];
		} GenericReparseBuffer;
	};
} REPARSE_DATA_BUFFER;

#define IO_REPARSE_TAG_MOUNT_POINT  0xA0000003L
#define IO_REPARSE_TAG_SYMLINK      0xA000000CL
#define IO_REPARSE_TAG_LX_SYMLINK   0xA000001DL
#define SYMLINK_FLAG_RELATIVE       1
#define FSCTL_GET_REPARSE_POINT     0x000900A8
#define FSCTL_SET_REPARSE_POINT     0x000900A4
#define FSCTL_PIPE_PEEK             0x0011400C
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE 16384

typedef struct _FILE_PIPE_PEEK_BUFFER {
	ULONG NamedPipeState;
	ULONG ReadDataAvailable;
	ULONG NumberOfMessages;
	ULONG MessageLength;
	char Data[1];
} FILE_PIPE_PEEK_BUFFER;

/* ---- processes ------------------------------------------------------- */
typedef enum _PROCESSINFOCLASS {
	ProcessBasicInformation = 0,
	ProcessQuotaLimits = 1,
	ProcessIoCounters = 2,
	ProcessVmCounters = 3,
	ProcessTimes = 4,
	ProcessBasePriority = 5,
	ProcessRaisePriority = 6,
	ProcessDebugPort = 7,
	ProcessExceptionPort = 8,
	ProcessAccessToken = 9,
	ProcessLdtInformation = 10,
	ProcessLdtSize = 11,
	ProcessDefaultHardErrorMode = 12,
	ProcessIoPortHandlers = 13,
	ProcessPooledUsageAndLimits = 14,
	ProcessWorkingSetWatch = 15,
	ProcessUserModeIOPL = 16,
	ProcessEnableAlignmentFaultFixup = 17,
	ProcessPriorityClass = 18,
	ProcessWx86Information = 19,
	ProcessHandleCount = 20,
	ProcessAffinityMask = 21,
	ProcessPriorityBoost = 22,
	ProcessDeviceMap = 23,
	ProcessSessionInformation = 24,
	ProcessForegroundInformation = 25,
	ProcessWow64Information = 26,
	ProcessImageFileName = 27,
	ProcessLUIDDeviceMapsEnabled = 28,
	ProcessBreakOnTermination = 29,
	ProcessDebugObjectHandle = 30,
	ProcessDebugFlags = 31,
	ProcessHandleTracing = 32,
	ProcessIoPriority = 33,
	ProcessExecuteFlags = 34,
	ProcessImageFileNameWin32 = 43,
	ProcessCookie = 36,
	ProcessImageInformation = 37
} PROCESSINFOCLASS;

typedef struct _PROCESS_BASIC_INFORMATION {
	NTSTATUS ExitStatus;
	PPEB PebBaseAddress;
	ULONG_PTR AffinityMask;
	LONG BasePriority;
	ULONG_PTR UniqueProcessId;
	ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION;

typedef struct _KERNEL_USER_TIMES {
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER ExitTime;
	LARGE_INTEGER KernelTime;
	LARGE_INTEGER UserTime;
} KERNEL_USER_TIMES;

typedef struct _SECTION_IMAGE_INFORMATION {
	PVOID TransferAddress;
	ULONG ZeroBits;
	SIZE_T MaximumStackSize;
	SIZE_T CommittedStackSize;
	ULONG SubSystemType;
	union {
		struct {
			USHORT SubSystemMinorVersion;
			USHORT SubSystemMajorVersion;
		};
		ULONG SubSystemVersion;
	};
	union {
		struct {
			USHORT MajorOperatingSystemVersion;
			USHORT MinorOperatingSystemVersion;
		};
		ULONG OperatingSystemVersion;
	};
	USHORT ImageCharacteristics;
	USHORT DllCharacteristics;
	USHORT Machine;
	BOOLEAN ImageContainsCode;
	union {
		UCHAR ImageFlags;
		struct {
			UCHAR ComPlusNativeReady : 1;
			UCHAR ComPlusILOnly : 1;
			UCHAR ImageDynamicallyRelocated : 1;
			UCHAR ImageMappedFlat : 1;
			UCHAR BaseBelow4gb : 1;
			UCHAR ComPlusPrefer32bit : 1;
			UCHAR Reserved : 2;
		};
	};
	ULONG LoaderFlags;
	ULONG ImageFileSize;
	ULONG CheckSum;
} SECTION_IMAGE_INFORMATION;

typedef struct _RTL_USER_PROCESS_INFORMATION {
	ULONG Length;
	HANDLE Process;
	HANDLE Thread;
	CLIENT_ID ClientId;
	SECTION_IMAGE_INFORMATION ImageInformation;
} RTL_USER_PROCESS_INFORMATION;

#define RTL_CLONE_PROCESS_FLAGS_CREATE_SUSPENDED  0x00000001
#define RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES   0x00000002
#define RTL_CLONE_PROCESS_FLAGS_NO_SYNCHRONIZE    0x00000004

#define PROCESS_TERMINATE           0x0001
#define PROCESS_CREATE_THREAD       0x0002
#define PROCESS_VM_OPERATION        0x0008
#define PROCESS_VM_READ             0x0010
#define PROCESS_VM_WRITE            0x0020
#define PROCESS_DUP_HANDLE          0x0040
#define PROCESS_QUERY_INFORMATION   0x0400
#define PROCESS_SUSPEND_RESUME      0x0800
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_ALL_ACCESS          (STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0xFFFF)

/* ---- job objects (src/misc/resource.c: setrlimit()'s NT analogue for
 * RLIMIT_NPROC/RLIMIT_CPU/RLIMIT_AS/RLIMIT_DATA) --------------------------
 * Field/flag layout and JOBOBJECTINFOCLASS ordinals per winnt.h; this
 * library only ever uses JobObjectBasicLimitInformation and
 * JobObjectExtendedLimitInformation, so nothing past those two is
 * declared here. */
#define JOB_OBJECT_ASSIGN_PROCESS   0x0001
#define JOB_OBJECT_SET_ATTRIBUTES   0x0002
#define JOB_OBJECT_QUERY            0x0004
#define JOB_OBJECT_TERMINATE        0x0008
#define JOB_OBJECT_ALL_ACCESS       (STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0x3f)

typedef enum _JOBOBJECTINFOCLASS {
	JobObjectBasicAccountingInformation = 1,
	JobObjectBasicLimitInformation = 2,
	JobObjectExtendedLimitInformation = 9
} JOBOBJECTINFOCLASS;

typedef struct _IO_COUNTERS {
	ULONGLONG ReadOperationCount;
	ULONGLONG WriteOperationCount;
	ULONGLONG OtherOperationCount;
	ULONGLONG ReadTransferCount;
	ULONGLONG WriteTransferCount;
	ULONGLONG OtherTransferCount;
} IO_COUNTERS;

typedef struct _JOBOBJECT_BASIC_LIMIT_INFORMATION {
	LARGE_INTEGER PerProcessUserTimeLimit;
	LARGE_INTEGER PerJobUserTimeLimit;
	ULONG LimitFlags;
	SIZE_T MinimumWorkingSetSize;
	SIZE_T MaximumWorkingSetSize;
	ULONG ActiveProcessLimit;
	ULONG_PTR Affinity;
	ULONG PriorityClass;
	ULONG SchedulingClass;
} JOBOBJECT_BASIC_LIMIT_INFORMATION;

typedef struct _JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
	JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
	IO_COUNTERS IoInfo;
	SIZE_T ProcessMemoryLimit;
	SIZE_T JobMemoryLimit;
	SIZE_T PeakProcessMemoryUsed;
	SIZE_T PeakJobMemoryUsed;
} JOBOBJECT_EXTENDED_LIMIT_INFORMATION;

#define JOB_OBJECT_LIMIT_PROCESS_TIME    0x00000002
#define JOB_OBJECT_LIMIT_ACTIVE_PROCESS  0x00000008
#define JOB_OBJECT_LIMIT_PROCESS_MEMORY  0x00000100

#define DUPLICATE_CLOSE_SOURCE      0x00000001
#define DUPLICATE_SAME_ACCESS       0x00000002
#define DUPLICATE_SAME_ATTRIBUTES   0x00000004

#define MEM_COMMIT                  0x00001000
#define MEM_RESERVE                 0x00002000
#define MEM_DECOMMIT                0x00004000
#define MEM_RELEASE                 0x00008000
#define MEM_FREE                    0x00010000
#define MEM_TOP_DOWN                0x00100000
#define PAGE_NOACCESS               0x01
#define PAGE_READONLY               0x02
#define PAGE_READWRITE              0x04
#define PAGE_EXECUTE                0x10
#define PAGE_EXECUTE_READ           0x20
#define PAGE_EXECUTE_READWRITE      0x40

#define HEAP_NO_SERIALIZE           0x00000001
#define HEAP_GROWABLE               0x00000002
#define HEAP_GENERATE_EXCEPTIONS    0x00000004
#define HEAP_ZERO_MEMORY            0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY  0x00000010

typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemBasicInformation = 0,
	SystemProcessorInformation = 1,
	SystemPerformanceInformation = 2,
	SystemTimeOfDayInformation = 3,
	SystemProcessInformation = 5,
	SystemProcessorPerformanceInformation = 8
} SYSTEM_INFORMATION_CLASS;

typedef struct _SYSTEM_BASIC_INFORMATION {
	ULONG Reserved;
	ULONG TimerResolution;
	ULONG PageSize;
	ULONG NumberOfPhysicalPages;
	ULONG LowestPhysicalPageNumber;
	ULONG HighestPhysicalPageNumber;
	ULONG AllocationGranularity;
	ULONG_PTR MinimumUserModeAddress;
	ULONG_PTR MaximumUserModeAddress;
	ULONG_PTR ActiveProcessorsAffinityMask;
	char NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION;

typedef struct _SYSTEM_TIMEOFDAY_INFORMATION {
	LARGE_INTEGER BootTime;
	LARGE_INTEGER CurrentTime;
	LARGE_INTEGER TimeZoneBias;
	ULONG TimeZoneId;
	ULONG Reserved;
	ULONGLONG BootTimeBias;
	ULONGLONG SleepTimeBias;
} SYSTEM_TIMEOFDAY_INFORMATION;

typedef struct _TIME_FIELDS {
	short Year;
	short Month;
	short Day;
	short Hour;
	short Minute;
	short Second;
	short Milliseconds;
	short Weekday;
} TIME_FIELDS;

typedef struct _RTL_TIME_ZONE_INFORMATION {
	LONG Bias;
	WCHAR StandardName[32];
	TIME_FIELDS StandardDate;
	LONG StandardBias;
	WCHAR DaylightName[32];
	TIME_FIELDS DaylightDate;
	LONG DaylightBias;
} RTL_TIME_ZONE_INFORMATION;

typedef struct _RTL_OSVERSIONINFOW {
	ULONG dwOSVersionInfoSize;
	ULONG dwMajorVersion;
	ULONG dwMinorVersion;
	ULONG dwBuildNumber;
	ULONG dwPlatformId;
	WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW;

typedef enum _THREADINFOCLASS {
	ThreadBasicInformation = 0,
	ThreadTimes = 1,
	ThreadPriority = 2,
	ThreadBasePriority = 3,
	ThreadAffinityMask = 4,
	ThreadImpersonationToken = 5,
	ThreadDescriptorTableEntry = 6,
	ThreadEnableAlignmentFaultFixup = 7,
	ThreadEventPair = 8,
	ThreadQuerySetWin32StartAddress = 9,
	ThreadZeroTlsCell = 10,
	ThreadPerformanceCount = 11,
	ThreadAmILastThread = 12,
	ThreadIdealProcessor = 13,
	ThreadPriorityBoost = 14,
	ThreadSetTlsArrayAddress = 15,
	ThreadIsIoPending = 16,
	ThreadHideFromDebugger = 17
} THREADINFOCLASS;

typedef struct _THREAD_BASIC_INFORMATION {
	NTSTATUS ExitStatus;
	PVOID TebBaseAddress;
	CLIENT_ID ClientId;
	ULONG_PTR AffinityMask;
	LONG Priority;
	LONG BasePriority;
} THREAD_BASIC_INFORMATION;

/* Exceptions */
typedef struct _EXCEPTION_RECORD {
	ULONG ExceptionCode;
	ULONG ExceptionFlags;
	struct _EXCEPTION_RECORD *ExceptionRecord;
	PVOID ExceptionAddress;
	ULONG NumberParameters;
	ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS {
	EXCEPTION_RECORD *ExceptionRecord;
	PVOID ContextRecord;
} EXCEPTION_POINTERS;

typedef LONG (NTAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *);
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_EXECUTE_HANDLER 1

#define EXCEPTION_ACCESS_VIOLATION          0xC0000005
#define EXCEPTION_ILLEGAL_INSTRUCTION       0xC000001D
#define EXCEPTION_INT_DIVIDE_BY_ZERO        0xC0000094
#define EXCEPTION_INT_OVERFLOW              0xC0000095
#define EXCEPTION_FLT_DIVIDE_BY_ZERO        0xC000008E
#define EXCEPTION_FLT_INVALID_OPERATION     0xC0000090
#define EXCEPTION_FLT_OVERFLOW              0xC0000091
#define EXCEPTION_STACK_OVERFLOW            0xC00000FD
#define EXCEPTION_BREAKPOINT                0x80000003
#define EXCEPTION_PRIV_INSTRUCTION          0xC0000096
#define EXCEPTION_IN_PAGE_ERROR             0xC0000006
#define EXCEPTION_DATATYPE_MISALIGNMENT     0x80000002
#define DBG_CONTROL_C                       0x40010005
#define DBG_CONTROL_BREAK                   0x40010008

typedef void (NTAPI *PIO_APC_ROUTINE)(PVOID, PIO_STATUS_BLOCK, ULONG);

/* ---- prototypes ------------------------------------------------------ */
NTSTATUS NTAPI NtClose(HANDLE);
NTSTATUS NTAPI NtCreateFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, LARGE_INTEGER *, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
NTSTATUS NTAPI NtOpenFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
NTSTATUS NTAPI NtReadFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, LARGE_INTEGER *, PULONG);
NTSTATUS NTAPI NtWriteFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, const void *, ULONG, LARGE_INTEGER *, PULONG);
NTSTATUS NTAPI NtQueryInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
NTSTATUS NTAPI NtSetInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS);
NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS, BOOLEAN, PUNICODE_STRING, BOOLEAN);
NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES, FILE_NETWORK_OPEN_INFORMATION *);
NTSTATUS NTAPI NtQueryAttributesFile(POBJECT_ATTRIBUTES, FILE_BASIC_INFORMATION *);
NTSTATUS NTAPI NtDeleteFile(POBJECT_ATTRIBUTES);
NTSTATUS NTAPI NtFlushBuffersFile(HANDLE, PIO_STATUS_BLOCK);
NTSTATUS NTAPI NtFsControlFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);
NTSTATUS NTAPI NtDeviceIoControlFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);
NTSTATUS NTAPI NtCreateNamedPipeFile(PHANDLE, ULONG, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, ULONG, LARGE_INTEGER *);
NTSTATUS NTAPI NtDuplicateObject(HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG);
NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
NTSTATUS NTAPI NtOpenProcess(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE, PUNICODE_STRING, PULONG);
NTSTATUS NTAPI NtQueryInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
NTSTATUS NTAPI NtSetInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
NTSTATUS NTAPI NtQueryInformationThread(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
NTSTATUS NTAPI NtWaitForSingleObject(HANDLE, BOOLEAN, LARGE_INTEGER *);
NTSTATUS NTAPI NtWaitForMultipleObjects(ULONG, HANDLE *, ULONG, BOOLEAN, LARGE_INTEGER *);
NTSTATUS NTAPI NtResumeThread(HANDLE, PULONG);
NTSTATUS NTAPI NtSuspendThread(HANDLE, PULONG);
NTSTATUS NTAPI NtGetContextThread(HANDLE, PVOID);
NTSTATUS NTAPI NtSetContextThread(HANDLE, PVOID);
NTSTATUS NTAPI NtReadVirtualMemory(HANDLE, PVOID, PVOID, SIZE_T, SIZE_T *);
NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE, PVOID, const void *, SIZE_T, SIZE_T *);
NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE, PVOID *, ULONG_PTR, SIZE_T *, ULONG, ULONG);
NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE, PVOID *, SIZE_T *, ULONG);
NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE, PVOID *, SIZE_T *, ULONG, PULONG);
NTSTATUS NTAPI NtQuerySystemTime(LARGE_INTEGER *);
NTSTATUS NTAPI NtSetSystemTime(LARGE_INTEGER *, LARGE_INTEGER *);
NTSTATUS NTAPI NtQueryPerformanceCounter(LARGE_INTEGER *, LARGE_INTEGER *);
NTSTATUS NTAPI NtDelayExecution(BOOLEAN, LARGE_INTEGER *);
NTSTATUS NTAPI NtYieldExecution(void);
NTSTATUS NTAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
NTSTATUS NTAPI NtCreateEvent(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG, BOOLEAN);
NTSTATUS NTAPI NtSetEvent(HANDLE, LONG *);
NTSTATUS NTAPI NtRaiseHardError(NTSTATUS, ULONG, ULONG, ULONG_PTR *, ULONG, PULONG);
NTSTATUS NTAPI NtCreateJobObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
NTSTATUS NTAPI NtAssignProcessToJobObject(HANDLE, HANDLE);
NTSTATUS NTAPI NtSetInformationJobObject(HANDLE, JOBOBJECTINFOCLASS, PVOID, ULONG);
NTSTATUS NTAPI NtWow64QueryInformationProcess64(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
NTSTATUS NTAPI NtWow64ReadVirtualMemory64(HANDLE, ULONGLONG, PVOID, ULONGLONG, ULONGLONG *);

PVOID    NTAPI RtlAllocateHeap(PVOID, ULONG, SIZE_T);
BOOLEAN  NTAPI RtlFreeHeap(PVOID, ULONG, PVOID);
PVOID    NTAPI RtlReAllocateHeap(PVOID, ULONG, PVOID, SIZE_T);
SIZE_T   NTAPI RtlSizeHeap(PVOID, ULONG, PVOID);
PVOID    NTAPI RtlCreateHeap(ULONG, PVOID, SIZE_T, SIZE_T, PVOID, PVOID);
PPEB     NTAPI RtlGetCurrentPeb(void);
BOOLEAN  NTAPI RtlDosPathNameToNtPathName_U(PCWSTR, PUNICODE_STRING, PCWSTR *, PVOID);
NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR, PUNICODE_STRING, PCWSTR *, PVOID);
void     NTAPI RtlFreeUnicodeString(PUNICODE_STRING);
void     NTAPI RtlInitUnicodeString(PUNICODE_STRING, PCWSTR);
ULONG    NTAPI RtlGetCurrentDirectory_U(ULONG, PWSTR);
NTSTATUS NTAPI RtlSetCurrentDirectory_U(PUNICODE_STRING);
ULONG    NTAPI RtlGetFullPathName_U(PCWSTR, ULONG, PWSTR, PWSTR *);
ULONG    NTAPI RtlDetermineDosPathNameType_U(PCWSTR);
ULONG    NTAPI RtlIsDosDeviceName_U(PCWSTR);
ULONG    NTAPI RtlNtStatusToDosError(NTSTATUS);
NTSTATUS NTAPI RtlUTF8ToUnicodeN(PWSTR, ULONG, PULONG, const char *, ULONG);
NTSTATUS NTAPI RtlUnicodeToUTF8N(char *, ULONG, PULONG, PCWSTR, ULONG);
NTSTATUS NTAPI RtlCreateProcessParameters(PRTL_USER_PROCESS_PARAMETERS *, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PVOID, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING);
NTSTATUS NTAPI RtlCreateProcessParametersEx(PRTL_USER_PROCESS_PARAMETERS *, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PVOID, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, ULONG);
NTSTATUS NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS);
NTSTATUS NTAPI RtlCreateUserProcess(PUNICODE_STRING, ULONG, PRTL_USER_PROCESS_PARAMETERS, PVOID, PVOID, HANDLE, BOOLEAN, HANDLE, HANDLE, RTL_USER_PROCESS_INFORMATION *);
NTSTATUS NTAPI RtlCloneUserProcess(ULONG, PVOID, PVOID, HANDLE, RTL_USER_PROCESS_INFORMATION *);
NTSTATUS NTAPI RtlQueryTimeZoneInformation(RTL_TIME_ZONE_INFORMATION *);
void     NTAPI RtlTimeToTimeFields(LARGE_INTEGER *, TIME_FIELDS *);
BOOLEAN  NTAPI RtlTimeFieldsToTime(TIME_FIELDS *, LARGE_INTEGER *);
NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOW *);
ULONG    NTAPI RtlRandomEx(PULONG);
PVOID    NTAPI RtlAddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER);
ULONG    NTAPI RtlRemoveVectoredExceptionHandler(PVOID);
void     NTAPI RtlRaiseStatus(NTSTATUS);
NTSTATUS NTAPI RtlQueryEnvironmentVariable_U(PVOID, PUNICODE_STRING, PUNICODE_STRING);
NTSTATUS NTAPI RtlCreateEnvironment(BOOLEAN, PVOID *);
NTSTATUS NTAPI RtlDestroyEnvironment(PVOID);
NTSTATUS NTAPI RtlSetEnvironmentVariable(PVOID *, PUNICODE_STRING, PUNICODE_STRING);
ULONG    NTAPI RtlDosSearchPath_U(PCWSTR, PCWSTR, PCWSTR, ULONG, PWSTR, PWSTR *);
NTSTATUS NTAPI LdrGetDllHandle(PWSTR, PVOID, PUNICODE_STRING, PVOID *);
NTSTATUS NTAPI LdrGetProcedureAddress(PVOID, PSTRING, ULONG, PVOID *);
NTSTATUS NTAPI LdrLoadDll(PWSTR, PULONG, PUNICODE_STRING, PVOID *);
NTSTATUS NTAPI LdrUnloadDll(PVOID);
void     NTAPI RtlAcquirePebLock(void);
void     NTAPI RtlReleasePebLock(void);

#endif
