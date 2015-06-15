#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "tdi.h"

extern "C" int SGXQueueTransfer(void *hTransferContext,
                                struct tag *psQueueTransfer) {
    static int (*__SGXQueueTransfer)(void *, struct tag *) = NULL;

    if (__SGXQueueTransfer == NULL) {
        __SGXQueueTransfer =
            (int (*)(void *, struct tag *))dlsym(RTLD_NEXT, "SGXQueueTransfer");
        if (NULL == __SGXQueueTransfer) {
            fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        }
    }

    TDITRACE("@T+SGXQueueTransfer()");
    int ret = __SGXQueueTransfer(hTransferContext, psQueueTransfer);
    TDITRACE("@T-SGXQueueTransfer()");

    return ret;
}

/*
 *  0xc01c6700  ENUM_DEVICES
 *  0xc01c6701  ACQUIRE_DEVICEINFO
 *  0xc01c6702  RELEASE_DEVICEINFO
 *  0xc01c6703  CREATE_DEVMEMCONTEXT
 *  0xc01c6704  DESTROY_DEVMEMCONTEXT
 *  0xc01c6705  GET_DEVMEM_HEAPINFO
 *  0xc01c6706  ALLOC_DEVICEMEM
 *  0xc01c6707  FREE_DEVICEMEM
 *  0xc01c6708  GETFREE_DEVICEMEM
 *  0xc01c6709  CREATE_COMMANDQUEUE
 *  0xc01c670a  DESTROY_COMMANDQUEUE
 *  0xc01c670b  MHANDLE_TO_MMAP_DATA
 *  0xc01c670c  CONNECT_SERVICES
 *  0xc01c670d  DISCONNECT_SERVICES
 *  0xc01c670e  WRAP_DEVICE_MEM
 *  0xc01c670f  GET_DEVICEMEMINFO
 *  0xc01c6710  RESERVE_DEV_VIRTMEM
 *  0xc01c6711  FREE_DEV_VIRTMEM
 *  0xc01c6712  MAP_EXT_MEMORY
 *  0xc01c6713  UNMAP_EXT_MEMORY
 *  0xc01c6714  MAP_DEV_MEMORY
 *  0xc01c6715  UNMAP_DEV_MEMORY
 *  0xc01c6716  MAP_DEVICECLASS_MEMORY
 *  0xc01c6717  UNMAP_DEVICECLASS_MEMORY
 *  0xc01c6718  MAP_MEM_INFO_TO_USER
 *  0xc01c6719  UNMAP_MEM_INFO_FROM_USER
 *  0xc01c671a  EXPORT_DEVICEMEM
 *  0xc01c671b  RELEASE_MMAP_DATA
 *  0xc01c671c  CHG_DEV_MEM_ATTRIBS
 *  0xc01c671d  MAP_DEV_MEMORY_2
 *  0xc01c671e  EXPORT_DEVICEMEM_2
 *  0xc01c671f  PROCESS_SIMISR_EVENT
 *  0xc01c6720  REGISTER_SIM_PROCESS
 *  0xc01c6721  UNREGISTER_SIM_PROCESS
 *  0xc01c6722  MAPPHYSTOUSERSPACE
 *  0xc01c6723  UNMAPPHYSTOUSERSPACE
 *  0xc01c6724  GETPHYSTOUSERSPACEMAP
 *  0xc01c6725  GET_FB_STATS
 *  0xc01c6726  GET_MISC_INFO
 *  0xc01c6727  RELEASE_MISC_INFO
 *  0xc01c6728  GET_OEMJTABLE
 *  0xc01c6729  ENUM_CLASS
 *  0xc01c672a  OPEN_DISPCLASS_DEVICE
 *  0xc01c672b  CLOSE_DISPCLASS_DEVICE
 *  0xc01c672c  ENUM_DISPCLASS_FORMATS
 *  0xc01c672d  ENUM_DISPCLASS_DIMS
 *  0xc01c672e  GET_DISPCLASS_SYSBUFFER
 *  0xc01c672f  GET_DISPCLASS_INFO
 *  0xc01c6730  CREATE_DISPCLASS_SWAPCHAIN
 *  0xc01c6731  DESTROY_DISPCLASS_SWAPCHAIN
 *  0xc01c6732  SET_DISPCLASS_DSTRECT
 *  0xc01c6733  SET_DISPCLASS_SRCRECT
 *  0xc01c6734  SET_DISPCLASS_DSTCOLOURKEY
 *  0xc01c6735  SET_DISPCLASS_SRCCOLOURKEY
 *  0xc01c6736  GET_DISPCLASS_BUFFERS
 *  0xc01c6737  SWAP_DISPCLASS_TO_BUFFER
 *  0xc01c6738  SWAP_DISPCLASS_TO_BUFFER2
 *  0xc01c6739  SWAP_DISPCLASS_TO_SYSTEM
 *  0xc01c673a  OPEN_BUFFERCLASS_DEVICE
 *  0xc01c673b  CLOSE_BUFFERCLASS_DEVICE
 *  0xc01c673c  GET_BUFFERCLASS_INFO
 *  0xc01c673d  GET_BUFFERCLASS_BUFFER
 *  0xc01c673e  WRAP_EXT_MEMORY
 *  0xc01c673f  UNWRAP_EXT_MEMORY
 *  0xc01c6740  ALLOC_SHARED_SYS_MEM
 *  0xc01c6741  FREE_SHARED_SYS_MEM
 *  0xc01c6742  MAP_MEMINFO_MEM
 *  0xc01c6743  UNMAP_MEMINFO_MEM
 *  0xc01c6744  INITSRV_CONNECT
 *  0xc01c6745  INITSRV_DISCONNECT
 *  0xc01c6746  EVENT_OBJECT_WAIT
 *  0xc01c6747  EVENT_OBJECT_OPEN
 *  0xc01c6748  EVENT_OBJECT_CLOSE
 *  0xc01c6749  CREATE_SYNC_INFO_MOD_OBJ
 *  0xc01c674a  DESTROY_SYNC_INFO_MOD_OBJ
 *  0xc01c674b  MODIFY_PENDING_SYNC_OPS
 *  0xc01c674c  MODIFY_COMPLETE_SYNC_OPS
 *  0xc01c674d  SYNC_OPS_TAKE_TOKEN
 *  0xc01c674e  SYNC_OPS_FLUSH_TO_TOKEN
 *  0xc01c674f  SYNC_OPS_FLUSH_TO_MOD_OBJ
 *  0xc01c6750  SYNC_OPS_FLUSH_TO_DELTA
 *  0xc01c6751  ALLOC_SYNC_INFO
 *  0xc01c6752  FREE_SYNC_INFO
 *  0xc01c6753
 *  0xc01c6754  SGX_GETCLIENTINFO);
 *  0xc01c6755  SGX_RELEASECLIENTINFO);
 *  0xc01c6756  SGX_GETINTERNALDEVINFO);
 *  0xc01c6757  SGX_DOKICK);
 *  0xc01c6758  SGX_GETPHYSPAGEADDR);
 *  0xc01c6759  SGX_READREGISTRYDWORD);
 *  0xc01c675a
 *  0xc01c675b
 *  0xc01c675c
 *  0xc01c675d  SGX_2DQUERYBLTSCOMPLETE
 *  0xc01c675e
 *  0xc01c675f
 *  0xc01c6760
 *  0xc01c6761  SGX_SUBMITTRANSFER
 *  0xc01c6762  SGX_GETMISCINFO
 *  0xc01c6763  SGXINFO_FOR_SRVINIT
 *  0xc01c6764  SGX_DEVINITPART2
 *  0xc01c6765  SGX_FINDSHAREDPBDESC
 *  0xc01c6766  SGX_UNREFSHAREDPBDESC
 *  0xc01c6767  SGX_ADDSHAREDPBDESC
 *  0xc01c6768  SGX_REGISTER_HW_RENDER_CONTEXT
 *  0xc01c6769  SGX_FLUSH_HW_RENDER_TARGET
 *  0xc01c676a  SGX_UNREGISTER_HW_RENDER_CONTEXT
 *  0xc01c676b
 *  0xc01c676c
 *  0xc01c676d
 *  0xc01c676e  SGX_REGISTER_HW_TRANSFER_CONTEXT
 *  0xc01c676f  SGX_UNREGISTER_HW_TRANSFER_CONTEXT
 *  0xc01c6770  SGX_SCHEDULE_PROCESS_QUEUES
 *  0xc01c6771  SGX_READ_HWPERF_CB
 *  0xc01c6772  SGX_SET_RENDER_CONTEXT_PRIORITY
 *  0xc01c6773  SGX_SET_TRANSFER_CONTEXT_PRIORITY
 */

static const char *strings[] = {

    "ENUM_DEVICES", "ACQUIRE_DEVICEINFO", "RELEASE_DEVICEINFO",
    "CREATE_DEVMEMCONTEXT", "DESTROY_DEVMEMCONTEXT", "GET_DEVMEM_HEAPINFO",
    "ALLOC_DEVICEMEM", "FREE_DEVICEMEM", "GETFREE_DEVICEMEM",
    "CREATE_COMMANDQUEUE", "DESTROY_COMMANDQUEUE", "MHANDLE_TO_MMAP_DATA",
    "CONNECT_SERVICES", "DISCONNECT_SERVICES", "WRAP_DEVICE_MEM",
    "GET_DEVICEMEMINFO", "RESERVE_DEV_VIRTMEM", "FREE_DEV_VIRTMEM",
    "MAP_EXT_MEMORY", "UNMAP_EXT_MEMORY", "MAP_DEV_MEMORY", "UNMAP_DEV_MEMORY",
    "MAP_DEVICECLASS_MEMORY", "UNMAP_DEVICECLASS_MEMORY",
    "MAP_MEM_INFO_TO_USER", "UNMAP_MEM_INFO_FROM_USER", "EXPORT_DEVICEMEM",
    "RELEASE_MMAP_DATA", "CHG_DEV_MEM_ATTRIBS", "MAP_DEV_MEMORY_2",
    "EXPORT_DEVICEMEM_2", "PROCESS_SIMISR_EVENT", "REGISTER_SIM_PROCESS",
    "UNREGISTER_SIM_PROCESS", "MAPPHYSTOUSERSPACE", "UNMAPPHYSTOUSERSPACE",
    "GETPHYSTOUSERSPACEMAP", "GET_FB_STATS", "GET_MISC_INFO",
    "RELEASE_MISC_INFO", "GET_OEMJTABLE", "ENUM_CLASS", "OPEN_DISPCLASS_DEVICE",
    "CLOSE_DISPCLASS_DEVICE", "ENUM_DISPCLASS_FORMATS", "ENUM_DISPCLASS_DIMS",
    "GET_DISPCLASS_SYSBUFFER", "GET_DISPCLASS_INFO",
    "CREATE_DISPCLASS_SWAPCHAIN", "DESTROY_DISPCLASS_SWAPCHAIN",
    "SET_DISPCLASS_DSTRECT", "SET_DISPCLASS_SRCRECT",
    "SET_DISPCLASS_DSTCOLOURKEY", "SET_DISPCLASS_SRCCOLOURKEY",
    "GET_DISPCLASS_BUFFERS", "SWAP_DISPCLASS_TO_BUFFER",
    "SWAP_DISPCLASS_TO_BUFFER2", "SWAP_DISPCLASS_TO_SYSTEM",
    "OPEN_BUFFERCLASS_DEVICE", "CLOSE_BUFFERCLASS_DEVICE",
    "GET_BUFFERCLASS_INFO", "GET_BUFFERCLASS_BUFFER", "WRAP_EXT_MEMORY",
    "UNWRAP_EXT_MEMORY", "ALLOC_SHARED_SYS_MEM", "FREE_SHARED_SYS_MEM",
    "MAP_MEMINFO_MEM", "UNMAP_MEMINFO_MEM", "INITSRV_CONNECT",
    "INITSRV_DISCONNECT", "EVENT_OBJECT_WAIT", "EVENT_OBJECT_OPEN",
    "EVENT_OBJECT_CLOSE", "CREATE_SYNC_INFO_MOD_OBJ",
    "DESTROY_SYNC_INFO_MOD_OBJ", "MODIFY_PENDING_SYNC_OPS",
    "MODIFY_COMPLETE_SYNC_OPS", "SYNC_OPS_TAKE_TOKEN",
    "SYNC_OPS_FLUSH_TO_TOKEN", "SYNC_OPS_FLUSH_TO_MOD_OBJ",
    "SYNC_OPS_FLUSH_TO_DELTA", "ALLOC_SYNC_INFO", "FREE_SYNC_INFO", "", "", "",
    "SGX_GETCLIENTINFO", "SGX_RELEASECLIENTINFO", "SGX_GETINTERNALDEVINFO",
    "SGX_DOKICK", "SGX_GETPHYSPAGEADDR", "SGX_READREGISTRYDWORD", "", "", "",
    "", "", "SGX_2DQUERYBLTSCOMPLETE", "", "", "", "", "", "SGX_SUBMITTRANSFER",
    "SGX_GETMISCINFO", "SGXINFO_FOR_SRVINIT", "SGX_DEVINITPART2",
    "SGX_FINDSHAREDPBDESC", "SGX_UNREFSHAREDPBDESC", "SGX_ADDSHAREDPBDESC",
    "SGX_REGISTER_HW_RENDER_CONTEXT", "SGX_FLUSH_HW_RENDER_TARGET",
    "SGX_UNREGISTER_HW_RENDER_CONTEXT", "", "", "", "", "",
    "SGX_REGISTER_HW_TRANSFER_CONTEXT", "SGX_UNREGISTER_HW_TRANSFER_CONTEXT",
    "SGX_SCHEDULE_PROCESS_QUEUES", "SGX_READ_HWPERF_CB",
    "SGX_SET_RENDER_CONTEXT_PRIORITY", "SGX_SET_TRANSFER_CONTEXT_PRIORITY"};

typedef struct package_tag { unsigned int id; } package_t;

#if 1
extern "C" int drmCommandWrite(int fd, unsigned long drmCommandIndex,
                               void *data, unsigned long size) {
    static int (*__drmCommandWrite)(int, unsigned long, void *, unsigned long) =
        NULL;

    if (__drmCommandWrite == NULL) {
        __drmCommandWrite =
            (int (*)(int, unsigned long, void *, unsigned long))dlsym(
                RTLD_NEXT, "drmCommandWrite");
        if (NULL == __drmCommandWrite) {
            fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        }
    }

    package_t *ppackage = (package_t *)data;

    TDITRACE("drmCommandWrite() \"%s\"", strings[ppackage->id - 0xc01c6700]);
    int ret = __drmCommandWrite(fd, drmCommandIndex, data, size);
    /*
     * cannot trace here...
     * TDITRACE("drmCommandWrite()");
     */
    return ret;
}
#endif
