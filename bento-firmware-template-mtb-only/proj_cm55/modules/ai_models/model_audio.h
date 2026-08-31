/*
* ImagiNet Compiler 5.5.3417.65534+eec6da02588e3e83732ce489840158da4d7ee938
* Copyright © 2023- Imagimob AB, All Rights Reserved.
* 
* Generated at 08/20/2025 05:44:03 UTC. Any changes will be lost.
* 
* Model ID  bac40391-2938-41b3-8a6a-6f93df208633
* 
* Memory    Size                      Efficiency
* Buffers   14360 bytes (RAM)         86 %
* State     47264 bytes (RAM)         100 %
* Readonly  103032 bytes (Flash)      100 %
* 
* Exported functions:
* 
*  @description: Try read data from model.
*  @param data_out Output features. Output float[2].
*  @return IPWIN_RET_SUCCESS (0) or IPWIN_RET_NODATA (-1), IPWIN_RET_ERROR (-2), IPWIN_RET_STREAMEND (-3)
*  int AIM_AUDIO_dequeue(float *data_out);
* 
*  @description: Try write data to model.
*  @param data_in Input features. Input float[1].
*  @return IPWIN_RET_SUCCESS (0) or IPWIN_RET_NODATA (-1), IPWIN_RET_ERROR (-2), IPWIN_RET_STREAMEND (-3)
*  int AIM_AUDIO_enqueue(const float *data_in);
* 
*  @description: Closes and flushes streams, free any heap allocated memory.
*  void AIM_AUDIO_finalize(void);
* 
*  @description: Initializes buffers to initial state.
*  @return IPWIN_RET_SUCCESS (0) or IPWIN_RET_NODATA (-1), IPWIN_RET_ERROR (-2), IPWIN_RET_STREAMEND (-3)
*  int AIM_AUDIO_init(void);
* 
* 
* Disclaimer:
*   The generated code relies on the optimizations done by the C compiler.
*   For example many for-loops of length 1 must be removed by the optimizer.
*   This can only be done if the functions are inlined and simplified.
*   Check disassembly if unsure.
*   tl;dr Compile using gcc with -O3 or -Ofast
* 
* Notes:
*     -> This code was generated with DEEPCRAFT Studio using:
*         ml-coretools 3.0.0.8583.
*         tensorflow 2.15.0.
*         ethos-u-vela 3.11.0.
*     -> This code requires the following Modus Toolbox libraries (add them to your
*     project using the Library Manager):
*         ml-middleware 3.1.0.
*         ml-tflite-micro 3.1.0.
*     -> This code requires the Modus Toolbox PSOC Edge E84 Early Access Pack (EAP)
*     version 0.3.2.5362.
*/

#include <stdbool.h>
#include <stdint.h>
#include "mtb_ml_model.h"
#define AIM_AUDIO_API_QUEUE

typedef int8_t q7_t;         // 8-bit fractional data type in Q1.7 format.
typedef int16_t q15_t;       // 16-bit fractional data type in Q1.15 format.
typedef int32_t q31_t;       // 32-bit fractional data type in Q1.31 format.
typedef int64_t q63_t;       // 64-bit fractional data type in Q1.63 format.

// Model GUID (16 bytes)
#define AIM_AUDIO_MODEL_ID {0x91, 0x03, 0xc4, 0xba, 0x38, 0x29, 0xb3, 0x41, 0x8a, 0x6a, 0x6f, 0x93, 0xdf, 0x20, 0x86, 0x33}


// First nibble is bit encoding, second nibble is number of bytes
#define IMAGINET_TYPES_NONE    (0x0)
#define IMAGINET_TYPES_FLOAT32    (0x14)
#define IMAGINET_TYPES_FLOAT64    (0x18)
#define IMAGINET_TYPES_INT8    (0x21)
#define IMAGINET_TYPES_INT16    (0x22)
#define IMAGINET_TYPES_INT32    (0x24)
#define IMAGINET_TYPES_INT64    (0x28)
#define IMAGINET_TYPES_Q7    (0x31)
#define IMAGINET_TYPES_Q15    (0x32)
#define IMAGINET_TYPES_Q31    (0x34)
#define IMAGINET_TYPES_BOOL    (0x41)
#define IMAGINET_TYPES_STRING    (0x54)
#define IMAGINET_TYPES_D8    (0x61)
#define IMAGINET_TYPES_D16    (0x62)
#define IMAGINET_TYPES_D32    (0x64)
#define IMAGINET_TYPES_UINT8    (0x71)
#define IMAGINET_TYPES_UINT16    (0x72)
#define IMAGINET_TYPES_UINT32    (0x74)
#define IMAGINET_TYPES_UINT64    (0x78)

// data_out [2] (8 bytes)
#define AIM_AUDIO_DATA_OUT_RANK (1)
#define AIM_AUDIO_DATA_OUT_SHAPE (((int[]){2})
#define AIM_AUDIO_DATA_OUT_COUNT (2)
#define AIM_AUDIO_DATA_OUT_TYPE float
#define AIM_AUDIO_DATA_OUT_TYPE_ID IMAGINET_TYPES_FLOAT32
#define AIM_AUDIO_DATA_OUT_SHIFT 4
#define AIM_AUDIO_DATA_OUT_OFFSET 0
#define AIM_AUDIO_DATA_OUT_SCALE 1
#define AIM_AUDIO_DATA_OUT_SYMBOLS {"unlabelled", "baby_cry"}

// data_in [1] (4 bytes)
#define AIM_AUDIO_DATA_IN_RANK (1)
#define AIM_AUDIO_DATA_IN_SHAPE (((int[]){1})
#define AIM_AUDIO_DATA_IN_COUNT (1)
#define AIM_AUDIO_DATA_IN_TYPE float
#define AIM_AUDIO_DATA_IN_TYPE_ID IMAGINET_TYPES_FLOAT32
#define AIM_AUDIO_DATA_IN_SHIFT 0
#define AIM_AUDIO_DATA_IN_OFFSET 0
#define AIM_AUDIO_DATA_IN_SCALE 1
#define AIM_AUDIO_DATA_IN_SYMBOLS { }

#define AIM_AUDIO_KEY_MAX (28)

// Return codes
#define AIM_AUDIO_RET_SUCCESS 0
#define AIM_AUDIO_RET_NODATA -1
#define AIM_AUDIO_RET_ERROR -2
#define AIM_AUDIO_RET_STREAMEND -3

#define IPWIN_RET_SUCCESS 0
#define IPWIN_RET_NODATA -1
#define IPWIN_RET_ERROR -2
#define IPWIN_RET_STREAMEND -3

// Exported methods
int AIM_AUDIO_dequeue(float *restrict data_out);
int AIM_AUDIO_enqueue(const float *restrict data_in);
void AIM_AUDIO_finalize(void);
int AIM_AUDIO_init(void);

// Implement this method to perform profiling    
void AIM_AUDIO_hook_region(bool entered, int32_t region_id);

// Symbol AIM_AUDIO_PROFILING must be defined to enable profiling of models
void AIM_AUDIO_mtb_models_profile_log();
#define AIM_AUDIO_MAX_MTB_MODELS 4
extern int32_t AIM_AUDIO_mtb_models_count;
extern mtb_ml_model_t* AIM_AUDIO_mtb_models[AIM_AUDIO_MAX_MTB_MODELS];

// Profiling regions
#ifdef AIM_AUDIO_PROFILING
    #define AIM_AUDIO_REGIONS_COUNT 2
    #define AIM_AUDIO_REGIONS_NAMES {\
        "PREPROCESSOR",\
        "NETWORK",\
    }
    typedef enum {
        AIM_AUDIO_PREPROCESSOR = 0,
        AIM_AUDIO_NETWORK = 1,
    } AIM_AUDIO_Region_t;
#else
    #define AIM_AUDIO_REGIONS_COUNT 0
    #define AIM_AUDIO_REGIONS_NAMES {}
    typedef enum {AIM_AUDIO_REGIONS_EMPTY} AIM_AUDIO_Region_t;
#endif

#ifdef AIM_AUDIO_REFLECTION

typedef enum {
    AIM_AUDIO_PARAM_UNDEFINED = 0,
    AIM_AUDIO_PARAM_INPUT = 1,
    AIM_AUDIO_PARAM_OUTPUT = 2,
    AIM_AUDIO_PARAM_REFERENCE = 3,
    AIM_AUDIO_PARAM_HANDLE = 7,
} AIM_AUDIO_param_attrib;

typedef char *label_text_t;

typedef struct {
    char* name;
    int size;
    label_text_t *labels;
} AIM_AUDIO_shape_dim;

typedef struct {
    char* name;
    AIM_AUDIO_param_attrib attrib;
    int32_t rank;
    AIM_AUDIO_shape_dim *shape;
    int32_t count;
    int32_t type_id;
    float frequency;
    int shift;
    float scale;
    long offset;
} AIM_AUDIO_param_def;

typedef enum {
    AIM_AUDIO_FUNC_ATTRIB_NONE = 0,
    AIM_AUDIO_FUNC_ATTRIB_CAN_FAIL = 1,
    AIM_AUDIO_FUNC_ATTRIB_PUBLIC = 2,
    AIM_AUDIO_FUNC_ATTRIB_INIT = 4,
    AIM_AUDIO_FUNC_ATTRIB_DESTRUCTOR = 8,
} AIM_AUDIO_func_attrib;

typedef struct {
    char* name;
    char* description;
    void* fn_ptr;
    AIM_AUDIO_func_attrib attrib;
    int32_t param_count;
    AIM_AUDIO_param_def *param_list;
} AIM_AUDIO_func_def;

typedef struct {
    uint32_t size;
    uint32_t peak_usage;
} AIM_AUDIO_mem_usage;

typedef enum {
    AIM_AUDIO_API_TYPE_UNDEFINED = 0,
    AIM_AUDIO_API_TYPE_FUNCTION = 1,
    AIM_AUDIO_API_TYPE_QUEUE = 2,
    AIM_AUDIO_API_TYPE_QUEUE_TIME = 3,
    AIM_AUDIO_API_TYPE_USER_DEFINED = 4,
} AIM_AUDIO_api_type;

typedef struct {
    uint32_t api_ver;
    uint8_t id[16];
    AIM_AUDIO_api_type api_type;
    char* prefix;
    AIM_AUDIO_mem_usage buffer_mem;
    AIM_AUDIO_mem_usage static_mem;
    AIM_AUDIO_mem_usage readonly_mem;
    int32_t func_count;
    AIM_AUDIO_func_def *func_list;
} AIM_AUDIO_api_def;

AIM_AUDIO_api_def *AIM_AUDIO_api(void);
#endif /* AIM_AUDIO_REFLECTION */

