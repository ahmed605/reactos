

#include <rxgkrnl.h>
#if 0
#include <reactos/rddm/rxgkinterface.h>
#include <reactos/rddm/rddm_private.h>
#include <debug.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;
extern DXGKRNL_INTERFACE DxgkrnlInterface;
#if 0
     KMTQAITYPE_UMDRIVERPRIVATE         =  0,
     KMTQAITYPE_UMDRIVERNAME            =  1,
     KMTQAITYPE_UMOPENGLINFO            =  2,
     KMTQAITYPE_GETSEGMENTSIZE          =  3,
     KMTQAITYPE_ADAPTERGUID             =  4,
     KMTQAITYPE_FLIPQUEUEINFO           =  5,
     KMTQAITYPE_ADAPTERADDRESS          =  6,
     KMTQAITYPE_SETWORKINGSETINFO       =  7,
     KMTQAITYPE_ADAPTERREGISTRYINFO     =  8,
     KMTQAITYPE_CURRENTDISPLAYMODE      =  9,
     KMTQAITYPE_MODELIST                = 10,
     KMTQAITYPE_CHECKDRIVERUPDATESTATUS = 11,
     KMTQAITYPE_VIRTUALADDRESSINFO      = 12, // _ADVSCH_
     KMTQAITYPE_DRIVERVERSION           = 13,

typedef struct _D3DKMT_QUERYADAPTERINFO
{
    D3DKMT_HANDLE           hAdapter;
    KMTQUERYADAPTERINFOTYPE Type;
    D3DKMT_PTR(VOID*,       pPrivateDriverData);
    UINT                    PrivateDriverDataSize;
} D3DKMT_QUERYADAPTERINFO;

    DXGKQAITYPE_UMDRIVERPRIVATE           = 0,
    DXGKQAITYPE_DRIVERCAPS                = 1,
    DXGKQAITYPE_QUERYSEGMENT              = 2,

typedef struct _DXGKARG_QUERYADAPTERINFO
{
    DXGK_QUERYADAPTERINFOTYPE   Type;
    VOID*                       pInputData;
    UINT                        InputDataSize;
    VOID*                       pOutputData;
    UINT                        OutputDataSize;
} DXGKARG_QUERYADAPTERINFO, *PDXGKARG_QUERYADAPTERINFO;
#endif

typedef struct _DXGK_QUERYSEGMENTIN {
  PHYSICAL_ADDRESS  AgpApertureBase;
  LARGE_INTEGER     AgpApertureSize;
  DXGK_SEGMENTFLAGS AgpFlags;
} DXGK_QUERYSEGMENTIN;

NTSTATUS
RxgKmtQueryAdapterInfo(_Inout_ const D3DKMT_QUERYADAPTERINFO* unnamedParam1)
{
    NTSTATUS Status = 0;
    BOOLEAN Callback;
    DXGKARG_QUERYADAPTERINFO QueryAdapterInfo;
        DXGK_QUERYSEGMENTIN SegmentInfo = {0};
        DXGK_QUERYSEGMENTOUT SegmentOut = {0};
        D3DKMT_ADAPTERREGISTRYINFO *RegistryInfo;
        WCHAR*   AdapterString = L"VirtualBox Graphics Adapter (WDDM)";
    VOID*    PrivateData;
    DPRINT1("RxgKmtQueryAdapterInfo: Entry\n");
    switch (unnamedParam1->Type)
    {
        case KMTQAITYPE_UMDRIVERPRIVATE:
            DPRINT1("KMTQAITYPE_UMDRIVERPRIVATE: Entry\n");
            QueryAdapterInfo.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
            QueryAdapterInfo.InputDataSize = unnamedParam1->PrivateDriverDataSize;
            QueryAdapterInfo.pInputData = unnamedParam1->pPrivateDriverData;
             QueryAdapterInfo.pOutputData = unnamedParam1->pPrivateDriverData; 
            Callback = TRUE;
            break;
        case KMTQAITYPE_GETSEGMENTSIZE:  
            DPRINT1("KMTQAITYPE_GETSEGMENTSIZE: Entry\n");
            QueryAdapterInfo.Type = DXGKQAITYPE_QUERYSEGMENT;
            QueryAdapterInfo.InputDataSize = sizeof(SegmentInfo);
            QueryAdapterInfo.pInputData = &SegmentInfo;
            QueryAdapterInfo.pOutputData = &SegmentOut;
             QueryAdapterInfo.OutputDataSize  = sizeof(DXGK_QUERYSEGMENTOUT);
            Callback = TRUE;
            break;
        case KMTQAITYPE_ADAPTERREGISTRYINFO:
            PrivateData = unnamedParam1->pPrivateDriverData;
            RegistryInfo = (D3DKMT_ADAPTERREGISTRYINFO *)PrivateData;
            RtlCopyMemory(RegistryInfo->AdapterString, AdapterString, (sizeof(WCHAR) * 36));
             DPRINT1("KMTQAITYPE_ADAPTERREGISTRYINFO: Entry\n");
            break;
        default:
            DPRINT1("Unknown KMT Query Type %X\n", unnamedParam1->Type);
            __debugbreak();
            break;
    }
    if (Callback)
        Status = RxgkDriverExtension->DxgkDdiQueryAdapterInfo(RxgkDriverExtension->MiniportContext,
                                                              &QueryAdapterInfo);

    if (unnamedParam1->Type == KMTQAITYPE_GETSEGMENTSIZE)
    {
        D3DKMT_SEGMENTSIZEINFO *SegmentKmtOut;
        SegmentKmtOut = (D3DKMT_SEGMENTSIZEINFO *)unnamedParam1->pPrivateDriverData;
        SegmentKmtOut->DedicatedSystemMemorySize = 0;
        SegmentKmtOut->DedicatedVideoMemorySize = 0;
        SegmentKmtOut->SharedSystemMemorySize = 0x10000 ;
        UNREFERENCED_PARAMETER(SegmentOut);
        __debugbreak();

    }
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("DxgkDdiQueryAdapterInfo: failed with Status %X\n", Status);
        __debugbreak();
    }
    else
        DPRINT1("DxgkDdiQueryAdapterInfo: Status %X\n", Status);
    return Status;
}
#endif