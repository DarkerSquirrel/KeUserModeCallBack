#include <ntifs.h>
#include <ntddk.h>
#include <ntimage.h>
#include "ZTY_DEFINE.h"
#include "IO.h"

#ifdef _WIN64

UCHAR gShellCode[] = "\x48\x83\xEC\x28"     //sub rsp,28h
                     "\x48\x89\xC8"         //mov rax,rcx
                     "\x48\x8B\x48\x08"     //mov rcx,qword ptr [rax+8]
                     "\x48\x8B\x50\x10"     //mov rdx,qword ptr [rax+10h]  
                     "\x4C\x8B\x40\x18"     //mov r8,qword ptr [rax+18h]
                     "\x4C\x8B\x48\x20"     //mov r9,qword ptr [rax+20h]  
                     "\x48\x8B\x00"         //mov rax,qword ptr [rax] 
                     "\xFF\xD0"             //call rax
                     "\x48\x83\xC4\x28"     //add rsp,28h
                     "\xC3"                 //ret
                     "\xCC";                //int 3

#else

UCHAR gShellCode[] = "\x8B\x44\x24\x04"     //mov eax,[esp+4]
					 "\xFF\x70\x10"         //push dword ptr [eax+0x10]
					 "\xFF\x70\x0C"         //push dword ptr [eax+0xC]
					 "\xFF\x70\x08"         //push dword ptr [eax+0x8]
					 "\xFF\x70\x04"         //push dword ptr [eax+0x4]
					 "\xFF\x10"             //call dword ptr [eax]
					 "\xC2\x08\x00"         //retn 8
					 "\xCC";                //int 3

#endif

CHAR gMsgText[] = "Msg From Driver��";

//��ָ��ģ��ĵ������ȡָ�����Ƶĺ�����ַ
ULONG_PTR GetProcAddressFromModule(ULONG_PTR ModuleAddress, CHAR *ProcName)
{
	PIMAGE_DOS_HEADER DosHeader = NULL;
	PIMAGE_NT_HEADERS NtHeader = NULL;
	PIMAGE_EXPORT_DIRECTORY ExportDirectory = NULL;
	
	ULONG *NameArry = NULL;													
	ULONG *AddressArry = NULL;											
	USHORT *OrdinalArry = NULL;

	ULONG Index = 0;

	DosHeader = (PIMAGE_DOS_HEADER)ModuleAddress;
	if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
		return 0;

	NtHeader = (PIMAGE_NT_HEADERS)(ModuleAddress + DosHeader->e_lfanew);
	if (NtHeader->Signature != IMAGE_NT_SIGNATURE)
		return 0;
	
	if (NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size == 0)
		return 0;

	ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)(ModuleAddress + (ULONG_PTR)NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	NameArry = (ULONG *)(ModuleAddress + ExportDirectory->AddressOfNames);
	AddressArry = (ULONG *)(ModuleAddress + ExportDirectory->AddressOfFunctions);
	OrdinalArry = (USHORT *)(ModuleAddress + ExportDirectory->AddressOfNameOrdinals);

	for (Index = 0; Index < ExportDirectory->AddressOfNames; ++Index)
	{
		if (NameArry[Index] != 0)
		{
			if (strcmp(ProcName, (CHAR*)(ModuleAddress + NameArry[Index])) == 0)
				return (ULONG_PTR)(ModuleAddress + AddressArry[OrdinalArry[Index]]);
		}
	}

	return 0;
}

//�ȴ�PEB�л�ȡָ�����Ƶ�DLL�Ļ�ַ
ULONG_PTR GetModuleHandleFromProcessPEB(PPEB Peb, CHAR *szDllName)
{
	PPEB_LDR_DATA pLdrData = NULL;
	PLDR_DATA_TABLE_ENTRY pLdrDataEntry = NULL;
	PLIST_ENTRY TempListItem = NULL;
	ANSI_STRING AnsiDllName = { 0 };

	ULONG_PTR DllBase = 0;
	
	pLdrData = Peb->Ldr;
	TempListItem = &pLdrData->InLoadOrderModuleList;

	for (TempListItem = TempListItem->Flink; TempListItem != &pLdrData->InLoadOrderModuleList; TempListItem = TempListItem->Flink)
	{
		pLdrDataEntry = (PLDR_DATA_TABLE_ENTRY)TempListItem;
		if (pLdrDataEntry->BaseDllName.Buffer)
		{
			RtlUnicodeStringToAnsiString(&AnsiDllName, &(pLdrDataEntry->BaseDllName), TRUE);
			if (strcmp(szDllName, AnsiDllName.Buffer) == 0)
			{
				DllBase = (ULONG)pLdrDataEntry->DllBase;
           	}
			RtlFreeAnsiString(&AnsiDllName);

			if (DllBase) 
				break;
		}
	}
	return DllBase;
}

PPEB GetCurrentProcessPeb()
{
	PROCESS_BASIC_INFORMATION BasicInfo = { 0 };
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	ULONG ReturenLength = 0;

	Status = ZwQueryInformationProcess(NtCurrentProcess(),
		ProcessBasicInformation,
		&BasicInfo,
		sizeof(PROCESS_BASIC_INFORMATION),
		&ReturenLength);

	if (NT_SUCCESS(Status))
		return BasicInfo.PebBaseAddress;

	return 0;
}

VOID Start()
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	ULONG_PTR AllocateSize = 0;
	PVOID ShellCodeBuffer = NULL;							//KeUserModeCallBackȥִ�е�ShellCode
	PVOID MsgText = NULL;									//MsgBox�������ַ����ĵ�ַ
	
	PPEB CurrentPeb = NULL; 								//��ǰ���̵�PEB
	ULONG_PTR KernelCallBackTable = 0;						//PEB�е�KernelCallBackTable�ĵ�ַ
	ULONG_PTR User32Base = 0;								//�ڵ�ǰ������User32�Ļ�ַ
	ULONG_PTR MsgBoxABase = 0;								//�ڵ�ǰ������MessageBoxA�Ļ�ַ

	ULONG ApiIndex = 0;
	ULONG_PTR Arguments[5] = { 0 };							//��һ���Ǻ�����ַ�������ĸ��ǲ���
	PVOID RetBuffer = 0;
	ULONG_PTR RetLength = 0;

	do
	{
        CurrentPeb = GetCurrentProcessPeb();
        if (CurrentPeb == NULL)
        {
            KdPrint(("��ȡPEBʧ�ܣ�\n"));
            break;
        }
        KernelCallBackTable = CurrentPeb->KernelCallbackTable;

        User32Base = GetModuleHandleFromProcessPEB(CurrentPeb, "USER32.dll");
        if (User32Base == 0)
        {
            KdPrint(("Ѱ��User32.dllʧ�ܣ�\n"));
            break;
        }

        MsgBoxABase = GetProcAddressFromModule(User32Base, "MessageBoxA");
        if (MsgBoxABase == 0)
        {
            KdPrint(("Ѱ��MessageBoxAʧ�ܣ�\n"));
            break;
        }

#ifdef _WIN64
        //64λ���е�����ģ���ΪApiIndexֻ��һ��ULONG���͵ģ�Ҳ����˵��shellcode�ĵ�ַҪ��kernelcallbacktable�ĵ�ַ������4G�ռ�
        //��32λ�£���ȫû������⣬������С�ڻ��Ǵ��ڶ�����ν��ȫ����4G�ռ��ڡ�
        //��64λ����Ҫ��kernelcallbacktable֮��Ŀռ�����ڴ���
        ShellCodeBuffer = (PVOID)(KernelCallBackTable + 0x100);
        AllocateSize = sizeof(gShellCode) + sizeof(ULONG_PTR);
        while (TRUE)
        {
            Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &ShellCodeBuffer, 0, (SIZE_T *)&AllocateSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (NT_SUCCESS(Status))
                break;

            ShellCodeBuffer = (PVOID)((ULONG_PTR)ShellCodeBuffer + 0x100);
        }
#else
		AllocateSize = sizeof(gShellCode) + sizeof(ULONG_PTR);
        Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &ShellCodeBuffer, 0, (SIZE_T *)&AllocateSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (FailStatus(Status))
		{
			KdPrint(("AllocateMemory Faile!,status is %x\n", Status));
			break;
		}
#endif
		RtlZeroMemory(ShellCodeBuffer, AllocateSize);
		RtlCopyMemory((PVOID)((ULONG_PTR)ShellCodeBuffer + sizeof(ULONG_PTR)), gShellCode, sizeof(gShellCode));
        *(ULONG_PTR*)ShellCodeBuffer = (ULONG_PTR)ShellCodeBuffer + sizeof(ULONG_PTR);

		AllocateSize = sizeof(gMsgText);
        Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &MsgText, 0, (SIZE_T *)&AllocateSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (FailStatus(Status))
		{
			KdPrint(("AllocateMemory Faile!,status is %x\n", Status));
			break;
		}
		RtlZeroMemory(MsgText, AllocateSize);
		RtlCopyMemory(MsgText, gMsgText, sizeof(gMsgText));

        ApiIndex = (ULONG)(((ULONG_PTR)ShellCodeBuffer - KernelCallBackTable) / sizeof(ULONG_PTR));
		Arguments[0] = MsgBoxABase;
		Arguments[1] = 0;							//hwnd = NULL
		Arguments[2] = (ULONG_PTR)MsgText;
		Arguments[3] = (ULONG_PTR)MsgText;
		Arguments[4] = 0;							//MB_OK

		Status = KeUserModeCallback(ApiIndex, Arguments, sizeof(Arguments), &RetBuffer, &RetLength);
		if (FailStatus(Status))
		{
			KdPrint(("����KeUserModeCallBackʧ�ܣ��������ǣ�%x��\n", Status));
			break; 
		}

	} while (FALSE);
}

VOID Unload(PDRIVER_OBJECT DriverObject)
{
	DeleteDevice(DriverObject);
	KdPrint(("Unload Success!\n"));
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegString)
{
	KdPrint(("Entry Driver!\n"));
	CreateDevice(DriverObject);
	DriverObject->DriverUnload = Unload;
	return STATUS_SUCCESS;
}