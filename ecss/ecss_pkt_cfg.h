/**
 * @file ecss_pkt_cfg.h
 *
 * @brief user configuration for the ECSS packet library, adapted from the
 *        ARIEL-OBSW PUS library; tracks ECSS-E-ST-70-41C as baseline
 */

#ifndef ECSS_PKT_CFG_H
#define ECSS_PKT_CFG_H


#define AP_PUS_VERSION		PUS_C_VERSION
#define AP_PKT_SIZE_MAX		4096

#define PUS_A_TC_SOURCE_ID_BIT	8
#define PUS_A_TC_SPARE_BITS	0
#define PUS_A_TM_USE_PKT_SUB_CNTR 0
#define PUS_A_TM_DEST_ID_BITS	8
#define PUS_A_TM_TIME_BITS	(5 * CHAR_BIT)
#define PUS_A_TM_SPARE_BITS	0

#define PUS_C_TC_SPARE_BITS	0
#define PUS_C_TM_SPARE_BITS	0
#define PUS_C_TM_TIME_OCTETS	6

#define ST_VER			1
#define ST_HK			3
#define ST_EVT			5
#define ST_Mem			6
#define ST_Time			9
#define ST_Ldt			13
#define ST_Tst			17
#define ST_ParamMngt		20
#define ST_Fdir			191
#define ST_Mode			193
#define ST_Algo			194
#define ST_Aocs			196
#define ST_Boot			197
#define ST_Proc			198
#define ST_Mngt			210
#define ST_Data			212
#define ST_Maint			213

#define SST_VER_SuccAccRep	1
#define SST_VER_FailedAccRep	2
#define SST_VER_SuccStartRep	3
#define SST_VER_FailedStartRep	4
#define SST_VER_SuccTermRep	7
#define SST_VER_FailedTermRep	8
#define SST_HK_CreHkCmd		1
#define SST_HK_DelHkCmd		3
#define SST_HK_EnbHkCmd		5
#define SST_HK_DisHkCmd		6
#define SST_HK_RepStructHkCmd	9
#define SST_HK_RepStructHkRep	10
#define SST_HK_Rep		25
#define SST_HK_OneShotHkCmd	27
#define SST_HK_AppParaCmd	29
#define SST_HK_ModHkPeriodCmd	31
#define SST_EVT_Rep		1
#define SST_EVT_EnbCmd		5
#define SST_EVT_DisCmd		6
#define SST_EVT_RepDisEvtCmd	7
#define SST_EVT_DisEvtRep	8
#define SST_EVT_SetEvtFilterCmd	138
#define SST_EVT_SetEvtSevCmd	140
#define SST_Mem_LoadCmd		2
#define SST_Mem_DumpCmd		5
#define SST_Mem_DumpRep		6
#define SST_Mem_CheckMemCmd	9
#define SST_Mem_CheckMemRep	10
#define SST_Mem_LoadRawMemCmd	11
#define SST_Mem_AbortMemDump	12
#define SST_Mem_enableMemProtCmd 15
#define SST_Mem_disableMemProtCmd 16
#define SST_Time_TimeUpdt	132
#define SST_Ldt_DownFirstRep	1
#define SST_Ldt_DownInterRep	2
#define SST_Ldt_DownLastRep	3
#define SST_Tst_AreYouAliveCmd	1
#define SST_Tst_AreYouAliveRep	2
#define SST_ParamMngt_ReportParamValuesCmd 1
#define SST_ParamMngt_ParamValueRep 2
#define SST_ParamMngt_SetParamValueCmd 3
#define SST_Fdir_FdCheckEnbGlobCmd 1
#define SST_Fdir_FdCheckDisGlobCmd 2
#define SST_Fdir_FdCheckEnbCmd	3
#define SST_Fdir_FdCheckDisCmd	4
#define SST_Fdir_FdRecovEnbGlobCmd 5
#define SST_Fdir_FdRecovDisGlobCmd 6
#define SST_Fdir_FdRecovEnbCmd	7
#define SST_Fdir_FdRecovDisCmd	8
#define SST_Mode_IaModePreOperCmd 1
#define SST_Mode_IaModeStrtOperCmd 2
#define SST_Mode_IaModeStpOperCmd 3
#define SST_Mode_IaModeGotoStbyCmd 4
#define SST_Mode_IaModeSimCmd	5
#define SST_Mode_IaModeContrSwOffCmd 6
#define SST_Mode_ResetDpuSafeCmd 255
#define SST_Algo_AlgoStrtCmd	1
#define SST_Algo_AlgoStopCmd	2
#define SST_Algo_AlgoSuspCmd	3
#define SST_Algo_AlgoResCmd	4
#define SST_Aocs_AocsRep	1
#define SST_Aocs_AocsCmd	2
#define SST_Aocs_AocsCalCmd	3
#define SST_Aocs_ModeCmd	4
#define SST_Boot_BootRep	1
#define SST_Boot_BootRepGenCmd	2
#define SST_Proc_ProcStartCmd	1
#define SST_Proc_ProcStopCmd	2
#define SST_Mngt_WatchdogEnbCmd	2
#define SST_Mngt_WatchdogDisCmd	3
#define SST_Mngt_LoadRegisterCmd 6
#define SST_Mngt_LoadRegisterArmCmd 7
#define SST_Mngt_LoadRegisterDisarmCmd 8
#define SST_Data_CopyCmd	1
#define SST_Data_ComprCmd	2
#define SST_Data_DecomprCmd	3
#define SST_Maint_SchedSegmCmd	1


#define PKT_FIELD_ENUMS(FLD) \
    FLD(PKT_FIELD_ID_FIRST = 0) \
    FLD(PKT_PLD_IS_ARBITRARY)  \
    FLD(PKT_HAS_CRC) \
    FLD(SuccAccRep_TcPcktVN) \
    FLD(SuccAccRep_TcPcktId) \
    FLD(SuccAccRep_TcPcktSeqCtrl) \
    FLD(FailedAccRep_TcPcktVN) \
    FLD(FailedAccRep_TcPcktId) \
    FLD(FailedAccRep_TcPcktSeqCtrl) \
    FLD(FailedAccRep_TcFailureCode) \
    FLD(SuccStartRep_TcPcktVN) \
    FLD(SuccStartRep_TcPcktId) \
    FLD(SuccStartRep_TcPcktSeqCtrl) \
    FLD(FailedStartRep_TcPcktVN) \
    FLD(FailedStartRep_TcPcktId) \
    FLD(FailedStartRep_TcPcktSeqCtrl) \
    FLD(FailedStartRep_TcFailureCode) \
    FLD(SuccTermRep_TcPcktVN) \
    FLD(SuccTermRep_TcPcktId) \
    FLD(SuccTermRep_TcPcktSeqCtrl) \
    FLD(FailedTermRep_TcPcktVN) \
    FLD(FailedTermRep_TcPcktId) \
    FLD(FailedTermRep_TcPcktSeqCtrl) \
    FLD(FailedTermRep_TcFailureCode) \
    FLD(CreHkCmd_SidNoCal) \
    FLD(CreHkCmd_Period) \
    FLD(CreHkCmd_NParam) \
    FLD(CreHkCmd_ParamId) \
    FLD(CreHkCmd_NFA) \
    FLD(DelHkCmd_NParam) \
    FLD(DelHkCmd_SidNoCal) \
    FLD(EnbHkCmd_NParam) \
    FLD(EnbHkCmd_SidNoCal) \
    FLD(DisHkCmd_NParam) \
    FLD(DisHkCmd_SidNoCal) \
    FLD(RepStructHkCmd_NParam) \
    FLD(RepStructHkCmd_SidNoCal) \
    FLD(RepStructHkRep_Sid) \
    FLD(RepStructHkRep_GenStat) \
    FLD(RepStructHkRep_Period) \
    FLD(RepStructHkRep_NParam) \
    FLD(RepStructHkRep_ParamId) \
    FLD(RepStructHkRep_NFA) \
    FLD(Rep_Sid) \
    FLD(OneShotHkCmd_NParam) \
    FLD(OneShotHkCmd_SidNoCal) \
    FLD(AppParaCmd_SidNoCal) \
    FLD(AppParaCmd_NParam) \
    FLD(AppParaCmd_ParamId) \
    FLD(AppParaCmd_NFA) \
    FLD(ModHkPeriodCmd_NParam) \
    FLD(ModHkPeriodCmd_SidNoCal) \
    FLD(ModHkPeriodCmd_Period) \
    FLD(Rep_event_id) \
    FLD(EnbCmd_NRep) \
    FLD(EnbCmd_event_id) \
    FLD(DisCmd_NRep) \
    FLD(DisCmd_event_id) \
    FLD(DisEvtRep_NRep) \
    FLD(DisEvtRep_event_id) \
    FLD(SetEvtFilterCmd_event_id) \
    FLD(SetEvtFilterCmd_Filter) \
    FLD(SetEvtSevCmd_event_id) \
    FLD(SetEvtSevCmd_Severity) \
    FLD(LoadCmd_MemoryId) \
    FLD(LoadCmd_Nrep) \
    FLD(LoadCmd_StartAddress) \
    FLD(LoadCmd_BlockLength) \
    FLD(LoadCmd_BlockData) \
    FLD(DumpCmd_MemoryId) \
    FLD(DumpCmd_Nrep) \
    FLD(DumpCmd_StartAddress) \
    FLD(DumpCmd_BlockLength) \
    FLD(DumpRep_MemoryId) \
    FLD(DumpRep_Nrep) \
    FLD(DumpRep_StartAddress) \
    FLD(DumpRep_BlockLength) \
    FLD(DumpRep_BlockData) \
    FLD(CheckMemCmd_MemoryId) \
    FLD(CheckMemCmd_Nrep) \
    FLD(CheckMemCmd_StartAddress) \
    FLD(CheckMemCmd_BlockLength) \
    FLD(CheckMemRep_MemoryId) \
    FLD(CheckMemRep_Nrep) \
    FLD(CheckMemRep_StartAddress) \
    FLD(CheckMemRep_BlockLength) \
    FLD(CheckMemRep_Checksum) \
    FLD(LoadRawMemCmd_MemoryId) \
    FLD(LoadRawMemCmd_StartAddress) \
    FLD(LoadRawMemCmd_bitMask) \
    FLD(LoadRawMemCmd_dataToLoad) \
    FLD(enableMemProtCmd_MemoryId) \
    FLD(disableMemProtCmd_MemoryId) \
    FLD(TimeUpdt_ObtTime) \
    FLD(DownFirstRep_SduId) \
    FLD(DownFirstRep_SduSeqNmb) \
    FLD(DownFirstRep_SduDataPartLength) \
    FLD(DownFirstRep_SduDataPart) \
    FLD(DownInterRep_SduId) \
    FLD(DownInterRep_SduSeqNmb) \
    FLD(DownInterRep_SduDataPartLength) \
    FLD(DownInterRep_SduDataPart) \
    FLD(DownLastRep_SduId) \
    FLD(DownLastRep_SduSeqNmb) \
    FLD(DownLastRep_SduDataPartLength) \
    FLD(DownLastRep_SduDataPart) \
    FLD(ReportParamValuesCmd_N) \
    FLD(ReportParamValuesCmd_ParamId) \
    FLD(ParamValueRep_N) \
    FLD(ParamValueRep_ParamId) \
    FLD(ParamValueRep_ParamValue) \
    FLD(SetParamValueCmd_N) \
    FLD(SetParamValueCmd_ParamId) \
    FLD(SetParamValueCmd_ParamValue) \
    FLD(FdCheckEnbCmd_FdirId) \
    FLD(FdCheckDisCmd_FdirId) \
    FLD(FdRecovEnbCmd_RpId) \
    FLD(FdRecovDisCmd_RpId) \
    FLD(AlgoStrtCmd_AlgoId) \
    FLD(AlgoStopCmd_AlgoId) \
    FLD(AlgoSuspCmd_AlgoId) \
    FLD(AlgoResCmd_AlgoId) \
    FLD(AocsRep_Mode) \
    FLD(AocsRep_Channel) \
    FLD(AocsRep_TimeTag) \
    FLD(AocsRep_TargetLocationX) \
    FLD(AocsRep_TargetLocationY) \
    FLD(AocsRep_MeasuredLocationX) \
    FLD(AocsRep_MeasuredLocationY) \
    FLD(AocsRep_TargetSignal) \
    FLD(AocsRep_IntegrationTime) \
    FLD(AocsRep_Validity) \
    FLD(AocsRep_QualityIndex) \
    FLD(AocsCmd_Mode) \
    FLD(AocsCmd_Channel) \
    FLD(AocsCmd_OBSID) \
    FLD(AocsCmd_TargetPosXFGS1) \
    FLD(AocsCmd_TargetPosYFGS1) \
    FLD(AocsCmd_TargetPosXFGS2) \
    FLD(AocsCmd_TargetPosYFGS2) \
    FLD(AocsCmd_MedianFilter) \
    FLD(AocsCmd_TargetSignalFGS1) \
    FLD(AocsCmd_TargetSignalFGS2) \
    FLD(AocsCmd_ShiftXmultFGS1) \
    FLD(AocsCmd_ShiftYmultFGS1) \
    FLD(AocsCmd_ShiftXaddFGS1) \
    FLD(AocsCmd_ShiftYaddFGS1) \
    FLD(AocsCmd_ShiftXmultFGS2) \
    FLD(AocsCmd_ShiftYmultFGS2) \
    FLD(AocsCmd_ShiftXaddFGS2) \
    FLD(AocsCmd_ShiftYaddFGS2) \
    FLD(AocsCalCmd_Mode) \
    FLD(AocsCalCmd_Channel) \
    FLD(AocsCalCmd_OBSID) \
    FLD(AocsCalCmd_TargetPosXFGS1) \
    FLD(AocsCalCmd_TargetPosYFGS1) \
    FLD(AocsCalCmd_TargetPosXFGS2) \
    FLD(AocsCalCmd_TargetPosYFGS2) \
    FLD(AocsCalCmd_MedianFilter) \
    FLD(AocsCalCmd_DeadPixMap) \
    FLD(AocsCalCmd_RefinedImgSizeFGS1) \
    FLD(AocsCalCmd_RefinedImgSizeFGS2) \
    FLD(AocsCalCmd_RebinningFactor) \
    FLD(AocsCalCmd_TargetSignalFGS1) \
    FLD(AocsCalCmd_TargetSignalFGS2) \
    FLD(AocsCalCmd_IterationFGS1) \
    FLD(AocsCalCmd_IterationFGS2) \
    FLD(AocsCalCmd_ShiftXmultFGS1) \
    FLD(AocsCalCmd_ShiftYmultFGS1) \
    FLD(AocsCalCmd_ShiftXaddFGS1) \
    FLD(AocsCalCmd_ShiftYaddFGS1) \
    FLD(AocsCalCmd_ShiftXmultFGS2) \
    FLD(AocsCalCmd_ShiftYmultFGS2) \
    FLD(AocsCalCmd_ShiftXaddFGS2) \
    FLD(AocsCalCmd_ShiftYaddFGS2) \
    FLD(AocsCalCmd_StartCropVisPhot) \
    FLD(AocsCalCmd_CropSizeVisPhot) \
    FLD(AocsCalCmd_StartCropNirSpec) \
    FLD(AocsCalCmd_CropSizeNirSpec) \
    FLD(ModeCmd_Mode) \
    FLD(BootRep_ResetEventId) \
    FLD(BootRep_EvtErrCnt) \
    FLD(BootRep_NoConnResetCnt) \
    FLD(BootRep_ResetTime) \
    FLD(BootRep_ResetTimeSync) \
    FLD(BootRep_TrapCore1) \
    FLD(BootRep_TrapCore2) \
    FLD(BootRep_SwTrapId) \
    FLD(BootRep_PsrCore1) \
    FLD(BootRep_WimCore1) \
    FLD(BootRep_PcCore1) \
    FLD(BootRep_NpcCore1) \
    FLD(BootRep_FsrCore1) \
    FLD(BootRep_PsrCore2) \
    FLD(BootRep_WimCore2) \
    FLD(BootRep_PcCore2) \
    FLD(BootRep_NpcCore2) \
    FLD(BootRep_FsrCore2) \
    FLD(BootRep_AhbStatusReg) \
    FLD(BootRep_AhbFailingAddrReg) \
    FLD(BootRep_PcHistCore1) \
    FLD(BootRep_PcHistCore2) \
    FLD(BootRep_BootSpare16) \
    FLD(BootRep_NErrRep) \
    FLD(BootRep_ErrTimeStamp) \
    FLD(BootRep_event_id) \
    FLD(BootRep_SquashCount) \
    FLD(BootRep_ErrLogInfo) \
    FLD(BootRepGenCmd_DpuMemoryId) \
    FLD(BootRepGenCmd_StartAddress) \
    FLD(BootRepGenCmd_BlockLength) \
    FLD(ProcStartCmd_ProcId) \
    FLD(ProcStopCmd_ProcId) \
    FLD(LoadRegisterCmd_RegAddr) \
    FLD(LoadRegisterCmd_RegData) \
    FLD(LoadRegisterCmd_VerifAddr) \
    FLD(LoadRegisterCmd_VerifMask) \
    FLD(CopyCmd_SrcMemId) \
    FLD(CopyCmd_SrcAddress) \
    FLD(CopyCmd_DataSize) \
    FLD(CopyCmd_TrgtMemId) \
    FLD(CopyCmd_TrgtAddress) \
    FLD(ComprCmd_SrcAddress) \
    FLD(ComprCmd_DstAddress) \
    FLD(ComprCmd_TmpWrkBufAddress) \
    FLD(ComprCmd_Lgth) \
    FLD(ComprCmd_CEKey) \
    FLD(DecomprCmd_SrcAddress) \
    FLD(DecomprCmd_DstAddress) \
    FLD(DecomprCmd_TmpWrkBufAddress) \
    FLD(DecomprCmd_Lgth) \
    FLD(DecomprCmd_CEKey) \
    FLD(SchedSegmCmd_ObjAddress) \
    FLD(PKT_FIELD_ID_LAST)

#define GEN_ENUM(ENUM) ENUM,
#define GEN_STRING(STRING) #STRING,


enum pkt_field_id {
    PKT_FIELD_ENUMS(GEN_ENUM)
};


#endif /* ECSS_PKT_CFG_H */