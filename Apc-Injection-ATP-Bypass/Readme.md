### Circumventing Windows Defender ATP's user-mode APC Injection sensor from Kernel-mode

This simple driver does the following :

   - It registers a CreateThreadNotifyRoutine in its DriverEntry.
   - CreateThreadNotifyRoutine queues a kernel-mode APC to a newly created thread.
   - The kernel-mode APC is delivered as soon as the IRQL drops below APC_LEVEL in the target thread in which we allocate executable memory in user-space, copy the shellcode, then queue the user-mode APC.
   - The user-mode APC is delivered in user-mode.

For more details : [link here]
