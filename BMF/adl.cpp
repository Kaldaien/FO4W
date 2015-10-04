#include "adl.h"

BOOL ADL_init = ADL_FALSE;

HINSTANCE hADL_DLL;

ADL_ADAPTER_NUMBEROFADAPTERS_GET   ADL_Adapter_NumberOfAdapters_Get;
ADL_ADAPTER_ADAPTERINFO_GET        ADL_Adapter_AdapterInfo_Get;

ADL_MAIN_CONTROL_CREATE            ADL_Main_Control_Create;
ADL_MAIN_CONTROL_DESTROY           ADL_Main_Control_Destroy;

ADL_OVERDRIVE5_TEMPERATURE_GET     ADL_Overdrive5_Temperature_Get;
ADL_OVERDRIVE5_FANSPEED_GET        ADL_Overdrive5_FanSpeed_Get;

ADL_OVERDRIVE5_CURRENTACTIVITY_GET ADL_Overdrive5_CurrentActivity_Get;

// Memory allocation function
void* __stdcall ADL_Main_Memory_Alloc ( int iSize )
{
  void* lpBuffer = malloc ( iSize );
  return lpBuffer;
}

// Optional Memory de-allocation function
void __stdcall ADL_Main_Memory_Free ( void** lpBuffer )
{
  if ( NULL != *lpBuffer )
  {
    free ( *lpBuffer );
    *lpBuffer = NULL;
  }
}

BOOL
BMF_InitADL (void)
{
  if (ADL_init != ADL_FALSE) {
    return ADL_init;
  }

  hADL_DLL = LoadLibrary (L"atiadlxx.dll");
  if (hADL_DLL == nullptr) {
    // A 32 bit calling application on 64 bit OS will fail to LoadLIbrary.
    // Try to load the 32 bit library (atiadlxy.dll) instead
    hADL_DLL = LoadLibrary (L"atiadlxy.dll");
  }

  if (hADL_DLL == nullptr) {
    ADL_init = ADL_FALSE - 1;
    return FALSE;
  }
  else {
    ADL_Main_Control_Create            =
      (ADL_MAIN_CONTROL_CREATE)GetProcAddress (
        hADL_DLL, "ADL_Main_Control_Create"
      );
    ADL_Main_Control_Destroy           =
      (ADL_MAIN_CONTROL_DESTROY)GetProcAddress (
         hADL_DLL, "ADL_Main_Control_Destroy"
       );

    ADL_Adapter_NumberOfAdapters_Get   =
     (ADL_ADAPTER_NUMBEROFADAPTERS_GET)GetProcAddress (
       hADL_DLL, "ADL_Adapter_NumberOfAdapters_Get"
     );

    ADL_Adapter_AdapterInfo_Get        =
     (ADL_ADAPTER_ADAPTERINFO_GET)GetProcAddress (
       hADL_DLL, "ADL_Adapter_AdapterInfo_Get"
     );

    ADL_Overdrive5_Temperature_Get     =
      (ADL_OVERDRIVE5_TEMPERATURE_GET)GetProcAddress (
        hADL_DLL, "ADL_Overdrive5_Temperature_Get"
      );

    ADL_Overdrive5_FanSpeed_Get        =
      (ADL_OVERDRIVE5_FANSPEED_GET)GetProcAddress (
        hADL_DLL, "ADL_Overdrive5_FanSpeed_Get"
      );

    ADL_Overdrive5_CurrentActivity_Get =
      (ADL_OVERDRIVE5_CURRENTACTIVITY_GET)GetProcAddress (
        hADL_DLL, "ADL_Overdrive5_CurrentActivity_Get"
      );

    if (ADL_Main_Control_Create            != nullptr &&
        ADL_Main_Control_Destroy           != nullptr &&
        ADL_Adapter_NumberOfAdapters_Get   != nullptr &&
        ADL_Adapter_AdapterInfo_Get        != nullptr &&
        ADL_Overdrive5_Temperature_Get     != nullptr &&
        ADL_Overdrive5_FanSpeed_Get        != nullptr &&
        ADL_Overdrive5_CurrentActivity_Get != nullptr) {
      if (ADL_OK == ADL_Main_Control_Create (ADL_Main_Memory_Alloc, 1)) {
        ADL_init = ADL_TRUE;
        return TRUE;
      }
    }
  }

  ADL_init = ADL_FALSE - 1;
  return FALSE;
}