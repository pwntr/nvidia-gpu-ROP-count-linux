#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#define NV_ALIGN_BYTES(size) __attribute__ ((aligned (size)))
#define NV_DECLARE_ALIGNED(TYPE_VAR, ALIGN) TYPE_VAR __attribute__ ((aligned (ALIGN)))

typedef unsigned __INT32_TYPE__ NvV32;
typedef unsigned __INT32_TYPE__ NvU32;
typedef NvU32 NvHandle;
typedef void* NvP64;
typedef uint64_t           NvU64;

#define NV_IOCTL_MAGIC      'F'
#define NV_IOCTL_BASE       200
#define NV_ESC_REGISTER_FD           (NV_IOCTL_BASE + 1)
#define NV_ESC_RM_CONTROL                           0x2A
#define NV_ESC_RM_ALLOC                             0x2B

#define NV01_DEVICE_0      (0x80U)
#define NV20_SUBDEVICE_0      (0x2080U)

#define CMD_SUBDEVICE_CTRL_GR_GET_ROP_INFO 0x20801213

typedef struct
{
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvV32    hClass;
    NvP64    pAllocParms NV_ALIGN_BYTES(8);
    NvU32    paramsSize;
    NvV32    status;
} NVOS21_PARAMETERS;

/* New struct with rights requested */
typedef struct
{
    NvHandle hRoot;                               // [IN] client handle
    NvHandle hObjectParent;                       // [IN] parent handle of new object
    NvHandle hObjectNew;                          // [INOUT] new object handle, 0 to generate
    NvV32    hClass;                              // [in] class num of new object
    NvP64    pAllocParms NV_ALIGN_BYTES(8);       // [IN] class-specific alloc parameters
    NvP64    pRightsRequested NV_ALIGN_BYTES(8);  // [IN] RS_ACCESS_MASK to request rights, or NULL
    NvU32    paramsSize;                          // [IN] Size of alloc params
    NvU32    flags;                               // [IN] flags for FINN serialization
    NvV32    status;                              // [OUT] status
} NVOS64_PARAMETERS;

typedef struct
{
    NvHandle hClient;
    NvHandle hObject;
    NvV32    cmd;
    NvU32    flags;
    NvP64    params NV_ALIGN_BYTES(8);
    NvU32    paramsSize;
    NvV32    status;
} NVOS54_PARAMETERS;

typedef struct
{
    NvU32    deviceId;
    NvHandle hClientShare;
    NvHandle hTargetClient;
    NvHandle hTargetDevice;
    NvV32    flags;
    NV_DECLARE_ALIGNED(NvU64 vaSpaceSize, 8);
    NV_DECLARE_ALIGNED(NvU64 vaStartInternal, 8);
    NV_DECLARE_ALIGNED(NvU64 vaLimitInternal, 8);
    NvV32    vaMode;
} NV0080_ALLOC_PARAMETERS;

typedef struct
{
    NvU32 subDeviceId;
} NV2080_ALLOC_PARAMETERS;

typedef struct
{
    NvU32 ropUnitCount;
    NvU32 ropOperationsFactor;
    NvU32 ropOperationsCount;
} NV2080_CTRL_GR_GET_ROP_INFO_PARAMS;


static bool open_nvidiactl(int* const nvidiactl_fd)
{
    *nvidiactl_fd = openat(AT_FDCWD, "/dev/nvidiactl", O_RDWR);
    return *nvidiactl_fd != -1;
}

static bool open_nvidia0(int nvidiactl_fd, int* const nvidia0_fd)
{
    *nvidia0_fd = openat(AT_FDCWD, "/dev/nvidia0", O_RDWR|O_CLOEXEC);
    if (*nvidia0_fd == -1)
        return false;
    return ioctl(*nvidia0_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_REGISTER_FD, sizeof(nvidiactl_fd)), &nvidiactl_fd) == 0;
}

static bool alloc_client(const int nvidiactl_fd, NvHandle* const hClient)
{
    NVOS21_PARAMETERS request;
    memset(&request, 0, sizeof(request));
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(request)), &request) != 0)
        return false;
    *hClient = request.hObjectNew;
    return request.status == 0;
}

static bool alloc_device(const int nvidiactl_fd, const NvHandle hClient, NvHandle* const hDevice)
{
    *hDevice = 0xB1000000;

    NV0080_ALLOC_PARAMETERS allocParams;
    memset(&allocParams, 0, sizeof(allocParams));

    NVOS64_PARAMETERS request = {
        .hRoot = hClient,
        .hObjectParent = hClient,
        .hObjectNew = *hDevice,
        .hClass = NV01_DEVICE_0,
        .pAllocParms = &allocParams,
        .pRightsRequested = NULL,
        .paramsSize = 0,
        .flags = 0,
        .status = 0
    };
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0)
        return false;
    return request.status == 0;
}

static bool alloc_subdevice(const int nvidiactl_fd, const NvHandle hClient, const NvHandle hParentDevice, NvHandle* const hSubDevice)
{
    *hSubDevice = 0xB2000000;

    NV2080_ALLOC_PARAMETERS allocParams;
    memset(&allocParams, 0, sizeof(allocParams));

    NVOS64_PARAMETERS request = {
        .hRoot = hClient,
        .hObjectParent = hParentDevice,
        .hObjectNew = *hSubDevice,
        .hClass = NV20_SUBDEVICE_0,
        .pAllocParms = &allocParams,
        .pRightsRequested = 0,
        .paramsSize = 0,
        .flags = 0,
        .status = 0
    };
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0)
        return false;
    return request.status == 0;
}

static bool get_rop_count(const int nvidiactl_fd, const NvHandle hClient, const NvHandle hSubdevice, NV2080_CTRL_GR_GET_ROP_INFO_PARAMS* ropParams)
{
    NVOS54_PARAMETERS request = {
        .hClient = hClient,
        .hObject = hSubdevice,
        .cmd = CMD_SUBDEVICE_CTRL_GR_GET_ROP_INFO,
        .flags = 0,
        .params = ropParams,
        .paramsSize = sizeof(NV2080_CTRL_GR_GET_ROP_INFO_PARAMS),
        .status = 0
    };
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_CONTROL, sizeof(NVOS54_PARAMETERS)), &request) != 0)
        return false;
    return request.status == 0;
}

int main()
{
    int nvidiactl_fd;
    if (!open_nvidiactl(&nvidiactl_fd))
    {
        fprintf(stderr, "Failed to open nvidiactl\n");
        return 1;
    }

    NvHandle hClient;
    if (!alloc_client(nvidiactl_fd, &hClient))
    {
        fprintf(stderr, "Failed to allocate client\n");
        return 1;
    }

    int nvidia0_fd;
    if (!open_nvidia0(nvidiactl_fd, &nvidia0_fd))
    {
        fprintf(stderr, "Failed to open nvidia0\n");
        return 1;
    }

    NvHandle hDevice;
    if (!alloc_device(nvidiactl_fd, hClient, &hDevice))
    {
        fprintf(stderr, "Failed to allocate device\n");
        return 1;
    }

    NvHandle hSubDevice;
    if (!alloc_subdevice(nvidiactl_fd, hClient, hDevice, &hSubDevice))
    {
        fprintf(stderr, "Failed to allocate subdevice\n");
        return 1;
    }

    NV2080_CTRL_GR_GET_ROP_INFO_PARAMS ropParams;
    memset(&ropParams, 0, sizeof(ropParams));
    if (!get_rop_count(nvidiactl_fd, hClient, hSubDevice, &ropParams))
    {
        fprintf(stderr, "Failed to get ROP count\n");
        return 1;
    }

    printf("ROP unit count: %d\n", ropParams.ropUnitCount);
    printf("ROP operations factor: %d\n", ropParams.ropOperationsFactor);
    printf("ROP operations count: %d\n", ropParams.ropOperationsCount);
    return 0;
}

