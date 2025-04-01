#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // For close()
#include <errno.h>  // For errno

#include <fcntl.h>
#include <sys/ioctl.h>

// --- Keep all original struct definitions and macros ---
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
    NvU32    deviceId; // <--- This will be used to specify the GPU index
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
    NvU32 subDeviceId; // Usually 0 for the primary subdevice
} NV2080_ALLOC_PARAMETERS;

typedef struct
{
    NvU32 ropUnitCount;
    NvU32 ropOperationsFactor;
    NvU32 ropOperationsCount;
} NV2080_CTRL_GR_GET_ROP_INFO_PARAMS;


// --- Core Functions (mostly unchanged, except open_nvidia_device) ---

static bool open_nvidiactl(int* const nvidiactl_fd)
{
    *nvidiactl_fd = openat(AT_FDCWD, "/dev/nvidiactl", O_RDWR);
    if (*nvidiactl_fd == -1) {
        perror("Failed to open /dev/nvidiactl");
        return false;
    }
    return true;
}

// Modified function to open a specific nvidia device by index
static bool open_nvidia_device(int nvidiactl_fd, int device_index, int* const nvidia_fd)
{
    char device_path[32]; // Should be enough for /dev/nvidiaXXX
    snprintf(device_path, sizeof(device_path), "/dev/nvidia%d", device_index);

    *nvidia_fd = openat(AT_FDCWD, device_path, O_RDWR|O_CLOEXEC);
    if (*nvidia_fd == -1) {
        // If the device doesn't exist, it's not necessarily an error,
        // it might just mean we've enumerated all devices.
        // Only print error for other failure reasons.
        if (errno != ENOENT) {
            fprintf(stderr, "Failed to open %s: %s\n", device_path, strerror(errno));
        }
        return false; // Indicate failure (could be ENOENT or other error)
    }

    // Register the control fd with the specific device fd
    if (ioctl(*nvidia_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_REGISTER_FD, sizeof(nvidiactl_fd)), &nvidiactl_fd) != 0) {
        perror("ioctl NV_ESC_REGISTER_FD failed");
        close(*nvidia_fd); // Close the fd on failure
        *nvidia_fd = -1;
        return false;
    }
    return true;
}


static bool alloc_client(const int nvidiactl_fd, NvHandle* const hClient)
{
    NVOS21_PARAMETERS request;
    memset(&request, 0, sizeof(request));
    // Set hObjectNew to 0 to let RM assign the handle
    request.hObjectNew = 0;

    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(request)), &request) != 0) {
        perror("ioctl NV_ESC_RM_ALLOC (client) failed");
        return false;
    }
    if (request.status != 0) {
        fprintf(stderr, "Failed to allocate client, RM status: 0x%x\n", request.status);
        return false;
    }
    *hClient = request.hObjectNew;
    return true;
}

// Modified to accept device_index
static bool alloc_device(const int nvidiactl_fd, const NvHandle hClient, int device_index, NvHandle* const hDevice)
{
    NV0080_ALLOC_PARAMETERS allocParams;
    memset(&allocParams, 0, sizeof(allocParams));
    // *** Set the deviceId to the target GPU index ***
    allocParams.deviceId = (NvU32)device_index;

    NVOS64_PARAMETERS request = {
        .hRoot = hClient,
        .hObjectParent = hClient,
        .hObjectNew = 0, // Let RM assign the handle
        .hClass = NV01_DEVICE_0,
        .pAllocParms = &allocParams,
        .pRightsRequested = NULL,
        // *** Set the correct size of the parameters ***
        .paramsSize = sizeof(NV0080_ALLOC_PARAMETERS),
        .flags = 0,
        .status = 0
    };

    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0) {
         perror("ioctl NV_ESC_RM_ALLOC (device) failed");
         return false;
    }
     if (request.status != 0) {
        fprintf(stderr, "GPU %d: Failed to allocate device, RM status: 0x%x\n", device_index, request.status);
        return false;
    }
    *hDevice = request.hObjectNew; // Store the assigned handle
    return true;
}

// No change needed here, uses parent handle
static bool alloc_subdevice(const int nvidiactl_fd, const NvHandle hClient, const NvHandle hParentDevice, NvHandle* const hSubDevice)
{
    NV2080_ALLOC_PARAMETERS allocParams;
    memset(&allocParams, 0, sizeof(allocParams));
    // Typically, we want the first (or only) subdevice, which is index 0.
    allocParams.subDeviceId = 0;

    NVOS64_PARAMETERS request = {
        .hRoot = hClient,
        .hObjectParent = hParentDevice, // Parent is the device handle
        .hObjectNew = 0, // Let RM assign the handle
        .hClass = NV20_SUBDEVICE_0,
        .pAllocParms = &allocParams,
        .pRightsRequested = 0,
        // *** Set the correct size of the parameters ***
        .paramsSize = sizeof(NV2080_ALLOC_PARAMETERS),
        .flags = 0,
        .status = 0
    };
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0) {
        perror("ioctl NV_ESC_RM_ALLOC (subdevice) failed");
        return false;
    }
    if (request.status != 0) {
        // Note: This error message won't inherently know the GPU index unless passed down
        fprintf(stderr, "Failed to allocate subdevice (parent handle 0x%x), RM status: 0x%x\n", hParentDevice, request.status);
        return false;
    }
    *hSubDevice = request.hObjectNew; // Store the assigned handle
    return true;
}

// No change needed here, uses handles
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
    if (ioctl(nvidiactl_fd, _IOC(_IOC_READ|_IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_CONTROL, sizeof(NVOS54_PARAMETERS)), &request) != 0) {
        perror("ioctl NV_ESC_RM_CONTROL (get_rop_count) failed");
        return false;
    }
    if (request.status != 0) {
        fprintf(stderr, "Failed to get ROP count (subdevice handle 0x%x), RM status: 0x%x\n", hSubdevice, request.status);
        return false;
    }
    return true;
}

int main()
{
    int ret_code = 0;
    int nvidiactl_fd = -1;
    NvHandle hClient = 0;
    int device_count = 0;

    if (!open_nvidiactl(&nvidiactl_fd)) {
        // Error already printed by open_nvidiactl
        return 1;
    }

    if (!alloc_client(nvidiactl_fd, &hClient)) {
        // Error already printed by alloc_client
        close(nvidiactl_fd);
        return 1;
    }

    // Loop through potential device indices
    for (int device_index = 0; ; ++device_index) {
        int nvidia_fd = -1;
        NvHandle hDevice = 0;
        NvHandle hSubDevice = 0;

        // --- Try to open the device ---
        if (!open_nvidia_device(nvidiactl_fd, device_index, &nvidia_fd)) {
            // If opening device 0 failed with ENOENT, no GPUs found
            // If opening later devices failed with ENOENT, we're done enumerating
            if (errno == ENOENT) {
                if (device_index == 0) {
                    fprintf(stderr, "No NVIDIA devices found (/dev/nvidia0 missing).\n");
                    ret_code = 1;
                } else {
                    printf("Found %d NVIDIA device(s).\n", device_count);
                }
                break; // Exit the loop
            } else {
                // Another error occurred during open (e.g., permissions)
                // Error message printed by open_nvidia_device
                ret_code = 1;
                break; // Exit the loop
            }
        }

        printf("--- Processing GPU %d /dev/nvidia%d ---\n", device_index, device_index);
        device_count++; // Increment count of successfully opened devices

        // --- Allocate Device Handle ---
        if (!alloc_device(nvidiactl_fd, hClient, device_index, &hDevice)) {
            fprintf(stderr, "GPU %d: Skipping due to device allocation failure.\n", device_index);
            close(nvidia_fd); // Close the specific device fd
            ret_code = 1;     // Mark as failure but continue to try next GPU
            continue;
        }

        // --- Allocate SubDevice Handle ---
        if (!alloc_subdevice(nvidiactl_fd, hClient, hDevice, &hSubDevice)) {
            fprintf(stderr, "GPU %d: Skipping due to subdevice allocation failure.\n", device_index);
            // NOTE: We should ideally free hDevice here using RmFree if this were
            // a long-running process. For this tool, we rely on process exit.
            close(nvidia_fd);
            ret_code = 1;
            continue;
        }

        // --- Get ROP Info ---
        NV2080_CTRL_GR_GET_ROP_INFO_PARAMS ropParams;
        memset(&ropParams, 0, sizeof(ropParams));
        if (!get_rop_count(nvidiactl_fd, hClient, hSubDevice, &ropParams)) {
            fprintf(stderr, "GPU %d: Skipping due to ROP count retrieval failure.\n", device_index);
            // NOTE: Ideally free hDevice, hSubDevice here.
            close(nvidia_fd);
            ret_code = 1;
            continue;
        }

        // --- Print Results (same format as original, prefixed with GPU index) ---
        printf("GPU %d ROP unit count: %d\n", device_index, ropParams.ropUnitCount);
        printf("GPU %d ROP operations factor: %d\n", device_index, ropParams.ropOperationsFactor);
        printf("GPU %d ROP operations count: %d\n", device_index, ropParams.ropOperationsCount);

        // --- Cleanup for this device ---
        // NOTE: Handles (hDevice, hSubDevice) are not explicitly freed here,
        // relying on process termination, similar to the original code.
        // If this caused issues, NVOS00_PARAMETERS with RmFree ioctl would be needed.
        close(nvidia_fd); // Close the /dev/nvidiaX file descriptor
    } // End of device loop

    // --- Final Cleanup ---
    // NOTE: hClient is also not explicitly freed.
    if (nvidiactl_fd != -1) {
        close(nvidiactl_fd);
    }

    // If no devices were found at all, return error code 1
    if (device_count == 0 && ret_code == 0) {
       // This condition might be hit if /dev/nvidia0 exists but fails alloc_device,
       // and no subsequent devices are found. Check if ret_code was already set.
       if(access("/dev/nvidia0", F_OK) != -1) {
           // If /dev/nvidia0 exists but we didn't process it, something went wrong earlier.
           // Error message should have been printed. Ensure error code is set.
           if (ret_code == 0) ret_code = 1;
       } else {
           // This case should have been caught by the ENOENT check inside the loop.
           fprintf(stderr, "No NVIDIA devices processed.\n");
           if (ret_code == 0) ret_code = 1;
       }
    }


    return ret_code;
}
