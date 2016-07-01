/*
Souhail Hammou : http://rce4fun.blogspot.com
Single-core only (multi-core version soon) [Windows 7 32-bit]
The SwapContext function is called by the thread
scheduler to switch the context from the current
thread to the next thread scheduled to run.
*/
#include <ntifs.h>
#include <ntddk.h>
extern "C" NTKERNELAPI void KiDispatchInterrupt(void);
int cnt = 0;
PUCHAR Processes[1024];
void DriverUnload(PDRIVER_OBJECT DriverObject){
	unsigned int saved_CR0;
	KIRQL Irql;
	PUCHAR p = (PUCHAR)KiDispatchInterrupt;
	unsigned int relative = *(unsigned int*)(p + 0xDE);
	PUCHAR KiSwapContext = (PUCHAR)((unsigned int)(p+0xDD) + relative + 5);
	//807e3900        cmp     byte ptr [esi+39h],0
	//7404            je      nt!SwapContext+0xa (828bdaea)
	char saved_ops[] = {0x80,0x7e,0x39,0x00,0x74,0x04};
	__asm{
		push eax
		mov eax,CR0
		mov saved_CR0,eax
		and eax,0xFFFEFFFF
		mov CR0,eax
		pop eax
	}
	Irql = KeRaiseIrqlToDpcLevel();
	for(int i=0;i<6;i++){
		KiSwapContext[i] = saved_ops[i];
	}
	KeLowerIrql(Irql);
	__asm{
		push eax
		mov eax,saved_CR0
		mov CR0,eax
		pop eax
	}
	/*Free the allocated pool blocks*/
	for(int i=0;i<cnt;i++){
		ExFreePool(Processes[i]);
	}
	return;
}
void GetProcessName(){
	PUCHAR pKTHREAD;
	PUCHAR ImageFileName;
	__asm{
		mov pKTHREAD,esi
	}
	PUCHAR Process = *(PUCHAR*)(pKTHREAD + 0x50);
	for(int i=0;i<cnt;i++){ 
		if(!strcmp((char*)Processes[i],(char*)Process+0x16c))
			return;
	}
	ImageFileName = (PUCHAR)ExAllocatePool(NonPagedPool,15);
	/*Copy the image name to an allocated space*/
	strcpy((char*)ImageFileName,(char*)(Process+0x16c));
	Processes[cnt] = ImageFileName;
	DbgPrint("%s\n",ImageFileName);
	cnt++;
}
//__fastcall SwapContext(PKTHREAD CurrentThread,PKTHREAD NextThread)
__declspec(naked) void HooK(){
	GetProcessName();
	/*before jumping back execute the overwritten functions*/
	//807e3900        cmp     byte ptr [esi+39h],0
	//7404            je      nt!SwapContext+0xa (828bdaea)
	__asm{
		cmp byte ptr[esi+39h],0
		/*je address , replaced in runtime*/
		_emit 0x0F
		_emit 0x84
		_emit 0xAA
		_emit 0xAA
		_emit 0xAA
		_emit 0xAA
		/*jmp just after the patched bytes*/
		_emit 0xE9
		_emit 0xBB
		_emit 0xBB
		_emit 0xBB
		_emit 0xBB
	}
}
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,PUNICODE_STRING RegistryPath){
	DbgPrint("SwapContext Hook to detect hidden processes\n");
	DbgPrint("Processes :");
	DriverObject->DriverUnload = DriverUnload;
	char detour_bytes[] = {0xe9,0xaa,0xbb,0xcc,0xdd,0x90};
	unsigned int saved_CR0;
	KIRQL Irql;
	/*KiDispatchInterrupt is exported*/
	PUCHAR p = (PUCHAR)KiDispatchInterrupt;
	PUCHAR det = (PUCHAR)HooK;
	int j;
	unsigned int relative = *(unsigned int*)(p + 0xDE);
	PUCHAR SwapContext = (PUCHAR)((unsigned int)(p+0xDD) + relative + 5);
	//DbgPrint("KiSwapContext at : %p\n",SwapContext);
	/*Implement the inline hook*/
	relative = (unsigned int)HooK - (unsigned int)SwapContext - 5;
	*(unsigned int*)&detour_bytes[1] = relative;
	/*Disable write protection*/
	__asm{
		push eax
		mov eax,CR0
		mov saved_CR0,eax
		and eax,0xFFFEFFFF
		mov CR0,eax
		pop eax
	}
	/*Set the detour function jump addresses in runtime*/
	for(j=0;;j++){
		if(det[j] == 0xAA && det[j+1] == 0xAA && det[j+2] == 0xAA && det[j+3] == 0xAA)
			break;
	}
	/*set the relative address for the conditional jump ()*/
	*(unsigned int*)&det[j] = (unsigned int)((SwapContext+0xa) - (det+j-2) - 6);
	/*set the relative address for the jump back to SwapContext*/
	for(;;j++){
		if(det[j] == 0xBB && det[j+1] == 0xBB && det[j+2] == 0xBB && det[j+3] == 0xBB)
			break;
	}
	*(unsigned int*)&det[j] = (unsigned int)((SwapContext + 6) - (det+j-1) - 5);
	/*Raise IRQL to patch safely*/
	Irql = KeRaiseIrqlToDpcLevel();
	/*implement the patch*/
	for(int i=0;i<6;i++){
		SwapContext[i] = detour_bytes[i];
	}
	KeLowerIrql(Irql);
	__asm{
		push eax
		mov eax,saved_CR0
		mov CR0,eax
		pop eax
	}
	return STATUS_SUCCESS;
}
