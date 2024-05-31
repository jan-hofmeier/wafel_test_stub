#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wafel/dynamic.h>
#include <wafel/ios_dynamic.h>
#include <wafel/utils.h>
#include <wafel/patch.h>
#include <wafel/ios/svc.h>
#include <wafel/trampoline.h>
#include <wafel/ios/ipc_types.h>

static u8 threadStack[0x1000] __attribute__((aligned(0x20)));
static bool ipcNodeKilled = false;
static u32 messageQueue[0x10];

int register_with_pm_stuff(char* node, int queueId){
    int ret;
    int pm_handle = ret = iosOpen("/dev/pm", 0);
    if(pm_handle < 0)
        return pm_handle;
    char *inputBuffer = iosAlloc(0xcaff, 0x21);
    if(!inputBuffer) goto out_close;
    memset(inputBuffer, 0, 0x21);
    strncpy(inputBuffer, node, 0x20);
    ret = iosIoctl(pm_handle, 0xe0, inputBuffer, 0x21, NULL, 0);
    if(ret < 0) goto out_free;
    memcpy(inputBuffer, &ret, 4);
    ret = iosRegisterResourceManager(node, queueId);
    if(ret) goto out_free;
    ret = iosIoctl(pm_handle, 0xe1, inputBuffer, 4, NULL, 0);

out_free:
    iosFree(0xcaff, inputBuffer);
out_close:
    iosClose(pm_handle);
    return ret;
}

int receive_loop(int queueId){
    ipcmessage *message;
    int res;
    debug_printf("TESTSTUB: Waiting for Messages\n");
    while ((res=iosReceiveMessage(queueId, &message, 0)) == 0) {
        debug_printf("TESTSTUB: /dev/testproc2 received: 0x%x\n", message->command);

        int res = 0;
        iosResourceReply(message, res);
    }
    debug_printf("TESTSTUB: END\n");
    return res;
}

u32 ipc_thread(void *) {
    ipcmessage *message;

    debug_printf("TESTSTUB: create Message Queue\n");
    int queueId = iosCreateMessageQueue(messageQueue, sizeof(messageQueue) / 4);


    debug_printf("TESTSTUB: register Device Node\n");
    register_with_pm_stuff("/dev/testproc1", queueId);
    if (register_with_pm_stuff("/dev/testproc2", queueId) == 0) {
        debug_printf("TESTSTUB: Waiting for Messages\n");
        while (iosReceiveMessage(queueId, &message, 0) == 0) {
            debug_printf("TESTSTUB: /dev/testproc2 received: 0x%x\n", message->command);

            int res = 0;
            iosResourceReply(message, res);
        }
    }
    debug_printf("TESTSTUB: destroy Message Queue\n");
    iosDestroyMessageQueue(queueId);
    return 0;
}


void print_state(trampoline_state *state){
    debug_printf("TESTREQUEST %p: r0: %p, r1: %p, r2: %p, r3: %p, r4: %p, r5: %p, r6: %p, r7: %p, r8: %p, r9: %p, r10: %p, r11: %p, r12: %p, lr: %p\n", state,
            state->r[0],state->r[1],state->r[2],state->r[3],state->r[4],state->r[5],state->r[6],state->r[7],state->r[8],state->r[9],state->r[10],state->r[11], state->r[12], state->lr);
    state->lr = 0xe4007834;
}


// This fn runs before everything else in kernel mode.
// It should be used to do extremely early patches
// (ie to BSP and kernel, which launches before MCP)
// It jumps to the real IOS kernel entry on exit.
__attribute__((target("arm")))
void kern_main()
{
    // Make sure relocs worked fine and mappings are good
    debug_printf("we in here trampoline demo plugin kern %p\n", kern_main);

    debug_printf("init_linking symbol at: %08x\n", wafel_find_symbol("init_linking"));

    //trampoline_hook_before(0xe40077e4, print_state);
    // ASM_PATCH_K(0xe40077e4, "push {r0-r12, lr}\n"
    //                         "mov r0, sp\n"
    //                         "ldr lr, hook_target\n"
    //                         "blx lr\n"
    //                         "pop {r0-r12, pc}\n"
    //                         "hook_target: .word print_state");

    // ASM_PATCH_K(0xe40077ac, "ldr lr, ret_target\n"
    //                         "ldr pc, hook_target\n"
    //                         "ret_target: .word 0xe40076c0\n"
    //                         "hook_target: .word receive_loop");

    // ASM_PATCH_K(0xe4007690, "ldr lr, ret_target\n"
    //                         "ldr pc, hook_target\n"
    //                         "ret_target: .word 0xe40076c8\n"
    //                         "hook_target: .word ipc_thread");


    //ASM_PATCH_K(0xe40168a4, "LDR pc, ipc_thread_ptr\nipc_thread_ptr: .word ipc_thread");
    ASM_PATCH_K(0xe40168a4, "bx lr");

    // U32_PATCH_K(0xe4043ba4, 0x42535342);
    // U32_PATCH_K(0xe4043bb4, 0x42535342);

}

// This fn runs before MCP's main thread, and can be used
// to perform late patches and spawn threads under MCP.
// It must return.
void mcp_main()
{
    int threadId = iosCreateThread(ipc_thread, 0, (u32 *) (threadStack + sizeof(threadStack)), sizeof(threadStack), 0x78, 1);
    debug_printf("TESTSTUB message threadId: %d\n", threadId);
    if (threadId >= 0)
        iosStartThread(threadId);

}
