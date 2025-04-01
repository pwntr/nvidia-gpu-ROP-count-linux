#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h> // Required for popen, pclose
#include <nvml.h>   // Include NVML header

#define NV_ALIGN_BYTES(size) __attribute__ ((aligned (size)))
#define NV_DECLARE_ALIGNED(TYPE_VAR, ALIGN) TYPE_VAR __attribute__ ((aligned (ALIGN)))

typedef unsigned __INT32_TYPE__ NvV32;
typedef unsigned __INT32_TYPE__ NvU32;
typedef NvU32 NvHandle;
typedef void* NvP64;
typedef uint64_t NvU64;

#define NV_IOCTL_MAGIC 'F'
#define NV_IOCTL_BASE 200
#define NV_ESC_REGISTER_FD (NV_IOCTL_BASE + 1)
#define NV_ESC_RM_CONTROL 0x2A
#define NV_ESC_RM_ALLOC 0x2B
#define NV_ESC_RM_FREE 0x2C

#define NV01_DEVICE_0 0x80U
#define NV20_SUBDEVICE_0 0x2080U

#define CMD_SUBDEVICE_CTRL_GR_GET_ROP_INFO 0x20801213

#define NV01_MEMORY_SYSTEM 0x300001
#define NV01_MEMORY_LOCAL 0x300002
#define NV_API_GET_RO_PROPERTY 0x5801
#define NV_RO_PROPERTY_STRING 0x00000001
#define NV_RO_PROPERTY_CHIPSET_NAME_STRING 0x00000002

typedef struct
{
	NvHandle hRoot;
	NvHandle hObjectParent;
	NvHandle hObjectNew;
	NvV32 hClass;
	NvP64 pAllocParms NV_ALIGN_BYTES(8);
	NvU32 paramsSize;
	NvV32 status;
} NVOS21_PARAMETERS;

typedef struct
{
	NvHandle hRoot;
	NvHandle hObjectParent;
	NvHandle hObjectNew;
	NvV32 hClass;
	NvP64 pAllocParms NV_ALIGN_BYTES(8);
	NvP64 pRightsRequested NV_ALIGN_BYTES(8);
	NvU32 paramsSize;
	NvU32 flags;
	NvV32 status;
} NVOS64_PARAMETERS;

typedef struct
{
	NvHandle hClient;
	NvHandle hObject;
	NvV32 cmd;
	NvU32 flags;
	NvP64 params NV_ALIGN_BYTES(8);
	NvU32 paramsSize;
	NvV32 status;
} NVOS54_PARAMETERS;

typedef struct
{
	NvU32 deviceId;
	NvHandle hClientShare;
	NvHandle hTargetClient;
	NvHandle hTargetDevice;
	NvV32 flags;
	NV_DECLARE_ALIGNED(NvU64 vaSpaceSize, 8);
	NV_DECLARE_ALIGNED(NvU64 vaStartInternal, 8);
	NV_DECLARE_ALIGNED(NvU64 vaLimitInternal, 8);
	NvV32 vaMode;
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

typedef struct
{
	NvU32 property;
	char* string;
} NV_API_PARAMETERS_GET_RO_PROPERTY;

static bool open_nvidiactl(int* const nvidiactl_fd)
{
	*nvidiactl_fd = openat(AT_FDCWD, "/dev/nvidiactl", O_RDWR);
	if (*nvidiactl_fd == -1) {
		perror("Failed to open /dev/nvidiactl");
		return false;
	}
	return true;
}

static bool open_nvidia_device(int nvidiactl_fd, int device_index, int* const nvidia_fd)
{
	char device_path[32];
	snprintf(device_path, sizeof(device_path), "/dev/nvidia%d", device_index);

	*nvidia_fd = openat(AT_FDCWD, device_path, O_RDWR | O_CLOEXEC);
	if (*nvidia_fd == -1) {
		if (errno != ENOENT) {
			fprintf(stderr, "Failed to open %s: %s\n", device_path, strerror(errno));
		}
		return false;
	}

	if (ioctl(*nvidia_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_REGISTER_FD, sizeof(nvidiactl_fd)), &nvidiactl_fd) != 0) {
		perror("ioctl NV_ESC_REGISTER_FD failed");
		close(*nvidia_fd);
		*nvidia_fd = -1;
		return false;
	}
	return true;
}

static bool alloc_client(const int nvidiactl_fd, NvHandle* const hClient)
{
	NVOS21_PARAMETERS request;
	memset(&request, 0, sizeof(request));
	request.hObjectNew = 0;

	if (ioctl(nvidiactl_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(request)), &request) != 0) {
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

static bool alloc_device(const int nvidiactl_fd, const NvHandle hClient, int device_index, NvHandle* const hDevice)
{
	NV0080_ALLOC_PARAMETERS allocParams;
	memset(&allocParams, 0, sizeof(allocParams));
	allocParams.deviceId = (NvU32)device_index;

	NVOS64_PARAMETERS request = {
		.hRoot = hClient,
		.hObjectParent = hClient,
		.hObjectNew = 0,
		.hClass = NV01_DEVICE_0,
		.pAllocParms = &allocParams,
		.pRightsRequested = NULL,
		.paramsSize = sizeof(NV0080_ALLOC_PARAMETERS),
		.flags = 0,
		.status = 0
	};

	if (ioctl(nvidiactl_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0) {
		perror("ioctl NV_ESC_RM_ALLOC (device) failed");
		return false;
	}
	if (request.status != 0) {
		fprintf(stderr, "GPU %d: Failed to allocate device, RM status: 0x%x\n", device_index, request.status);
		return false;
	}

	// Debug: Print handle value after allocation
	//    printf("GPU %d: hDevice allocated = 0x%x, status = 0x%x\n", device_index, request.hObjectNew, request.status);
	*hDevice = request.hObjectNew; // Store the handle *after* printing it!
	return true;
}

// Modified function signature
static bool alloc_subdevice(const int nvidiactl_fd, const NvHandle hClient, const NvHandle hParentDevice, int device_index, NvHandle* const hSubDevice)
{
	NV2080_ALLOC_PARAMETERS allocParams;
	memset(&allocParams, 0, sizeof(allocParams));
	allocParams.subDeviceId = 0;

	NVOS64_PARAMETERS request = {
		.hRoot = hClient,
		.hObjectParent = hParentDevice,
		.hObjectNew = 0,
		.hClass = NV20_SUBDEVICE_0,
		.pAllocParms = &allocParams,
		.pRightsRequested = 0,
		.paramsSize = sizeof(NV2080_ALLOC_PARAMETERS),
		.flags = 0,
		.status = 0
	};
	if (ioctl(nvidiactl_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, sizeof(NVOS64_PARAMETERS)), &request) != 0) {
		perror("ioctl NV_ESC_RM_ALLOC (subdevice) failed");
		return false;
	}
	if (request.status != 0) {
		fprintf(stderr, "Failed to allocate subdevice (parent handle 0x%x), RM status: 0x%x\n", hParentDevice, request.status);
		return false;
	}
	// Debug: Print handle value after allocation
	//    printf("GPU %d: hSubDevice allocated = 0x%x, status = 0x%x\n", device_index, request.hObjectNew, request.status);
	*hSubDevice = request.hObjectNew;
	return true;
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
	if (ioctl(nvidiactl_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_CONTROL, sizeof(NVOS54_PARAMETERS)), &request) != 0) {
		perror("ioctl NV_ESC_RM_CONTROL (get_rop_count) failed");
		return false;
	}
	if (request.status != 0) {
		fprintf(stderr, "Failed to get ROP count (subdevice handle 0x%x), RM status: 0x%x\n", hSubdevice, request.status);
		return false;
	}
	return true;
}


// NVML version of get_gpu_name
bool get_gpu_name_nvml(int device_index, char* gpu_name, size_t gpu_name_size) {
	nvmlReturn_t result;
	nvmlDevice_t device;

	// Initialize NVML (only needs to be done once per process)
	static bool nvml_initialized = false;
	if (!nvml_initialized) {
		result = nvmlInit();
		if (result != NVML_SUCCESS) {
			fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
			return false;  // NVML initialization failed, can't proceed
		}
		nvml_initialized = true;
	}

	result = nvmlDeviceGetHandleByIndex(device_index, &device);
	if (result != NVML_SUCCESS) {
		fprintf(stderr, "Failed to get device handle for index %d: %s\n", device_index, nvmlErrorString(result));
		return false;
	}

	result = nvmlDeviceGetName(device, gpu_name, gpu_name_size);
	if (result != NVML_SUCCESS) {
		fprintf(stderr, "Failed to get device name for index %d: %s\n", device_index, nvmlErrorString(result));
		return false;
	}

	return true;
}

static bool free_handle(const int nvidiactl_fd, const NvHandle hClient, const NvHandle hObject)
{
	NVOS21_PARAMETERS request;
	memset(&request, 0, sizeof(request));

	request.hRoot = hClient;
	request.hObjectParent = 0;
	request.hObjectNew = hObject;
	request.hClass = 0;
	request.pAllocParms = NULL;
	request.paramsSize = 0;
	request.status = 0;

	if (ioctl(nvidiactl_fd, _IOC(_IOC_READ | _IOC_WRITE, NV_IOCTL_MAGIC, NV_ESC_RM_FREE, sizeof(request)), &request) != 0) {
		//        perror("ioctl NV_ESC_RM_FREE failed");
		return false;
	}

	if (request.status != 0) {
		fprintf(stderr, "Failed to free handle 0x%x, RM status: 0x%x\n", hObject, request.status);
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
		return 1;
	}

	if (!alloc_client(nvidiactl_fd, &hClient)) {
		close(nvidiactl_fd);
		return 1;
	}

	for (int device_index = 0;; ++device_index) {
		int nvidia_fd = -1;
		NvHandle hDevice = 0;
		NvHandle hSubDevice = 0;
		char gpu_name[256] = {0};

		if (!open_nvidia_device(nvidiactl_fd, device_index, &nvidia_fd)) {
			if (errno == ENOENT) {
				if (device_index == 0) {
					fprintf(stderr, "No NVIDIA devices found (/dev/nvidia0 missing).\n");
					ret_code = 1;
				}
				else {
					printf("Found %d NVIDIA device(s).\n", device_count);
				}
				break;
			}
			else {
				ret_code = 1;
				break;
			}
		}

		printf("--- Processing GPU %d /dev/nvidia%d ---\n", device_index, device_index);
		device_count++;

		if (!alloc_device(nvidiactl_fd, hClient, device_index, &hDevice)) {
			fprintf(stderr, "GPU %d: Skipping due to device allocation failure.\n", device_index);
			close(nvidia_fd);
			ret_code = 1;
			continue;
		}


		// Get GPU Name using NVML
		if (!get_gpu_name_nvml(device_index, gpu_name, sizeof(gpu_name))) {
			fprintf(stderr, "GPU %d: Failed to get GPU name using NVML. Falling back to Unknown.\n", device_index);
			strncpy(gpu_name, "Unknown", sizeof(gpu_name));
			gpu_name[sizeof(gpu_name) - 1] = '\0';
		}


		// Modified call to alloc_subdevice
		if (!alloc_subdevice(nvidiactl_fd, hClient, hDevice, device_index, &hSubDevice)) {
			fprintf(stderr, "GPU %d: Skipping due to subdevice allocation failure.\n", device_index);
			close(nvidia_fd);
			free_handle(nvidiactl_fd, hClient, hDevice);
			ret_code = 1;
			continue;
		}

		NV2080_CTRL_GR_GET_ROP_INFO_PARAMS ropParams;
		memset(&ropParams, 0, sizeof(ropParams));
		if (!get_rop_count(nvidiactl_fd, hClient, hSubDevice, &ropParams)) {
			fprintf(stderr, "GPU %d: Skipping due to ROP count retrieval failure.\n", device_index);
			close(nvidia_fd);
			free_handle(nvidiactl_fd, hClient, hSubDevice);
			free_handle(nvidiactl_fd, hClient, hDevice);
			ret_code = 1;
			continue;
		}

		printf("Name: %s\n", gpu_name);
		printf("ROP unit count: %d\n", ropParams.ropUnitCount);
		printf("ROP operations factor: %d\n", ropParams.ropOperationsFactor);
		printf("ROP operations count: %d\n", ropParams.ropOperationsCount);

		// Debug: Print handle values before freeing
		//        printf("GPU %d: About to free hSubDevice = 0x%x, hDevice = 0x%x\n", device_index, hSubDevice, hDevice);

		// Free in reverse order of allocation
		free_handle(nvidiactl_fd, hClient, hSubDevice);
		free_handle(nvidiactl_fd, hClient, hDevice);
		close(nvidia_fd);
	}

	free_handle(nvidiactl_fd, hClient, hClient);

	// Shutdown NVML
	nvmlShutdown();

	if (nvidiactl_fd != -1) {
		close(nvidiactl_fd);
	}

	if (device_count == 0 && ret_code == 0) {
		if (access("/dev/nvidia0", F_OK) != -1) {
			if (ret_code == 0) ret_code = 1;
		}
		else {
			fprintf(stderr, "No NVIDIA devices processed.\n");
			if (ret_code == 0) ret_code = 1;
		}
	}

	return ret_code;
}
