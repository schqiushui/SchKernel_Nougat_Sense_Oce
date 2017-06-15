/*******************************************************************************
  Copyright � 2016, STMicroelectronics International N.V.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of STMicroelectronics nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 NON-INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS ARE DISCLAIMED.
 IN NO EVENT SHALL STMICROELECTRONICS INTERNATIONAL N.V. BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include "vl53l0_api.h"
#include "vl53l0_api_core.h"
#include "vl53l0_api_calibration.h"

#ifndef __KERNEL__
#include <stdlib.h>
#endif

#define LOG_FUNCTION_START(fmt, ...) \
    _LOG_FUNCTION_START(TRACE_MODULE_API, fmt, ##__VA_ARGS__)
#define LOG_FUNCTION_END(status, ...) \
    _LOG_FUNCTION_END(TRACE_MODULE_API, status, ##__VA_ARGS__)
#define LOG_FUNCTION_END_FMT(status, fmt, ...) \
    _LOG_FUNCTION_END_FMT(TRACE_MODULE_API, status, fmt, ##__VA_ARGS__)

#define REF_ARRAY_SPAD_0  0
#define REF_ARRAY_SPAD_5  5
#define REF_ARRAY_SPAD_10 10

uint32_t refArrayQuadrants[4] = {REF_ARRAY_SPAD_10, REF_ARRAY_SPAD_5,
    REF_ARRAY_SPAD_0, REF_ARRAY_SPAD_5 };

VL53L0_Error VL53L0_perform_xtalk_calibration(VL53L0_DEV Dev,
        FixPoint1616_t XTalkCalDistance,
        FixPoint1616_t *pXTalkCompensationRateMegaCps)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint16_t sum_ranging = 0;
    uint16_t sum_spads = 0;
    FixPoint1616_t sum_signalRate = 0;
    FixPoint1616_t total_count = 0;
    uint8_t xtalk_meas = 0;
    VL53L0_RangingMeasurementData_t RangingMeasurementData;
    FixPoint1616_t xTalkStoredMeanSignalRate;
    FixPoint1616_t xTalkStoredMeanRange;
    FixPoint1616_t xTalkStoredMeanRtnSpads;
    uint32_t signalXTalkTotalPerSpad;
    uint32_t xTalkStoredMeanRtnSpadsAsInt;
    uint32_t xTalkCalDistanceAsInt;
    FixPoint1616_t XTalkCompensationRateMegaCps;

    if (XTalkCalDistance <= 0)
        Status = VL53L0_ERROR_INVALID_PARAMS;

    
    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_SetXTalkCompensationEnable(Dev, 0);

    
    if (Status == VL53L0_ERROR_NONE) {
        Status = VL53L0_SetLimitCheckEnable(Dev,
                VL53L0_CHECKENABLE_RANGE_IGNORE_THRESHOLD, 0);
    }

    
    if (Status == VL53L0_ERROR_NONE) {
        sum_ranging = 0;
        sum_spads = 0;
        sum_signalRate = 0;
        total_count = 0;
        for (xtalk_meas = 0; xtalk_meas < 50; xtalk_meas++) {
            Status = VL53L0_PerformSingleRangingMeasurement(Dev,
                    &RangingMeasurementData);

            if (Status != VL53L0_ERROR_NONE)
                break;

            
            if (RangingMeasurementData.RangeStatus == 0) {
                sum_ranging = sum_ranging +
                    RangingMeasurementData.RangeMilliMeter;
                sum_signalRate = sum_signalRate +
                    RangingMeasurementData.SignalRateRtnMegaCps;
                sum_spads = sum_spads +
                    RangingMeasurementData.EffectiveSpadRtnCount
                    / 256;
                total_count = total_count + 1;
            }
        }

        
        if (total_count == 0)
            Status = VL53L0_ERROR_RANGE_ERROR;

    }


    if (Status == VL53L0_ERROR_NONE) {
        
        xTalkStoredMeanSignalRate = sum_signalRate / total_count;
        xTalkStoredMeanRange = (FixPoint1616_t)((uint32_t)(
                    sum_ranging << 16) / total_count);
        xTalkStoredMeanRtnSpads = (FixPoint1616_t)((uint32_t)(
                    sum_spads << 16) / total_count);

        xTalkStoredMeanRtnSpadsAsInt = (xTalkStoredMeanRtnSpads +
                0x8000) >> 16;

        xTalkCalDistanceAsInt = (XTalkCalDistance + 0x8000) >> 16;

        if (xTalkStoredMeanRtnSpadsAsInt == 0 ||
                xTalkCalDistanceAsInt == 0 ||
                xTalkStoredMeanRange >= XTalkCalDistance) {
            XTalkCompensationRateMegaCps = 0;
        } else {
            xTalkCalDistanceAsInt = (XTalkCalDistance +
                    0x8000) >> 16;

            signalXTalkTotalPerSpad = (xTalkStoredMeanSignalRate) /
                xTalkStoredMeanRtnSpadsAsInt;

            signalXTalkTotalPerSpad *= ((1 << 16) -
                    (xTalkStoredMeanRange / xTalkCalDistanceAsInt));

            
            XTalkCompensationRateMegaCps = (signalXTalkTotalPerSpad
                    + 0x8000) >> 16;
        }

        *pXTalkCompensationRateMegaCps = XTalkCompensationRateMegaCps;

        
        if (Status == VL53L0_ERROR_NONE)
            Status = VL53L0_SetXTalkCompensationEnable(Dev, 1);

        
        if (Status == VL53L0_ERROR_NONE)
            Status = VL53L0_SetXTalkCompensationRateMegaCps(Dev,
                    XTalkCompensationRateMegaCps);

    }

    return Status;
}

VL53L0_Error VL53L0_perform_offset_calibration(VL53L0_DEV Dev,
        FixPoint1616_t CalDistanceMilliMeter,
        int32_t *pOffsetMicroMeter)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint16_t sum_ranging = 0;
    FixPoint1616_t total_count = 0;
    VL53L0_RangingMeasurementData_t RangingMeasurementData;
    FixPoint1616_t StoredMeanRange;
    uint32_t StoredMeanRangeAsInt;
    uint32_t CalDistanceAsInt_mm;
    uint8_t SequenceStepEnabled;
    int meas = 0;

    if (CalDistanceMilliMeter <= 0)
        Status = VL53L0_ERROR_INVALID_PARAMS;

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_SetOffsetCalibrationDataMicroMeter(Dev, 0);


    
    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_GetSequenceStepEnable(Dev,
                VL53L0_SEQUENCESTEP_TCC, &SequenceStepEnabled);


    
    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_SetSequenceStepEnable(Dev,
                VL53L0_SEQUENCESTEP_TCC, 0);


    
    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_SetLimitCheckEnable(Dev,
                VL53L0_CHECKENABLE_RANGE_IGNORE_THRESHOLD, 0);

    
    if (Status == VL53L0_ERROR_NONE) {
        sum_ranging = 0;
        total_count = 0;
        for (meas = 0; meas < 50; meas++) {
            Status = VL53L0_PerformSingleRangingMeasurement(Dev,
                    &RangingMeasurementData);

            if (Status != VL53L0_ERROR_NONE)
                break;

            
            if (RangingMeasurementData.RangeStatus == 0) {
                sum_ranging = sum_ranging +
                    RangingMeasurementData.RangeMilliMeter;
                total_count = total_count + 1;
            }
        }

        
        if (total_count == 0)
            Status = VL53L0_ERROR_RANGE_ERROR;
    }


    if (Status == VL53L0_ERROR_NONE) {
        
        StoredMeanRange = (FixPoint1616_t)((uint32_t)(sum_ranging << 16)
                / total_count);

        StoredMeanRangeAsInt = (StoredMeanRange + 0x8000) >> 16;

        CalDistanceAsInt_mm = (CalDistanceMilliMeter + 0x8000) >> 16;

        *pOffsetMicroMeter = (CalDistanceAsInt_mm -
                StoredMeanRangeAsInt) * 1000;

        
        if (Status == VL53L0_ERROR_NONE) {
            VL53L0_SETPARAMETERFIELD(Dev, RangeOffsetMicroMeters,
                    *pOffsetMicroMeter);
            Status = VL53L0_SetOffsetCalibrationDataMicroMeter(Dev,
                    *pOffsetMicroMeter);
        }

    }

    
    if (Status == VL53L0_ERROR_NONE) {
        if (SequenceStepEnabled != 0)
            Status = VL53L0_SetSequenceStepEnable(Dev,
                    VL53L0_SEQUENCESTEP_TCC, 1);
    }

    return Status;
}


VL53L0_Error VL53L0_set_offset_calibration_data_micro_meter(VL53L0_DEV Dev,
        int32_t OffsetCalibrationDataMicroMeter)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    int32_t cMaxOffsetMicroMeter = 511000;
    int32_t cMinOffsetMicroMeter = -512000;
    int16_t cOffsetRange = 4096;
    uint32_t encodedOffsetVal;

    LOG_FUNCTION_START("");

    if (OffsetCalibrationDataMicroMeter > cMaxOffsetMicroMeter)
        OffsetCalibrationDataMicroMeter = cMaxOffsetMicroMeter;
    else if (OffsetCalibrationDataMicroMeter < cMinOffsetMicroMeter)
        OffsetCalibrationDataMicroMeter = cMinOffsetMicroMeter;

    if (OffsetCalibrationDataMicroMeter >= 0) {
        encodedOffsetVal =
            OffsetCalibrationDataMicroMeter/250;
    } else {
        encodedOffsetVal =
            cOffsetRange +
            OffsetCalibrationDataMicroMeter/250;
    }

    Status = VL53L0_WrWord(Dev,
            VL53L0_REG_ALGO_PART_TO_PART_RANGE_OFFSET_MM,
            encodedOffsetVal);

    LOG_FUNCTION_END(Status);
    return Status;
}

VL53L0_Error VL53L0_get_offset_calibration_data_micro_meter(VL53L0_DEV Dev,
        int32_t *pOffsetCalibrationDataMicroMeter)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint16_t RangeOffsetRegister;
    int16_t cMaxOffset = 2047;
    int16_t cOffsetRange = 4096;

    

    Status = VL53L0_RdWord(Dev,
            VL53L0_REG_ALGO_PART_TO_PART_RANGE_OFFSET_MM,
            &RangeOffsetRegister);

    if (Status == VL53L0_ERROR_NONE) {
        RangeOffsetRegister = (RangeOffsetRegister & 0x0fff);

        
        if (RangeOffsetRegister > cMaxOffset)
            *pOffsetCalibrationDataMicroMeter =
                (int16_t)(RangeOffsetRegister - cOffsetRange)
                * 250;
        else
            *pOffsetCalibrationDataMicroMeter =
                (int16_t)RangeOffsetRegister * 250;

    }

    return Status;
}


VL53L0_Error VL53L0_apply_offset_adjustment(VL53L0_DEV Dev)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    int32_t CorrectedOffsetMicroMeters;
    int32_t CurrentOffsetMicroMeters;

    Status = VL53L0_get_info_from_device(Dev, 7);

    
    if (Status == VL53L0_ERROR_NONE) {
        Status = VL53L0_GetOffsetCalibrationDataMicroMeter(Dev,
                &CurrentOffsetMicroMeters);
    }

    
    if (Status == VL53L0_ERROR_NONE) {

        
        PALDevDataSet(Dev, Part2PartOffsetNVMMicroMeter,
                CurrentOffsetMicroMeters);

        CorrectedOffsetMicroMeters = CurrentOffsetMicroMeters +
            (int32_t)PALDevDataGet(Dev,
                    Part2PartOffsetAdjustmentNVMMicroMeter);

        Status = VL53L0_SetOffsetCalibrationDataMicroMeter(Dev,
                CorrectedOffsetMicroMeters);

        
        if (Status == VL53L0_ERROR_NONE) {
            VL53L0_SETPARAMETERFIELD(Dev, RangeOffsetMicroMeters,
                    CorrectedOffsetMicroMeters);
        }
    }

    return Status;
}

void get_next_good_spad(uint8_t goodSpadArray[], uint32_t size,
        uint32_t curr, int32_t *next)
{
    uint32_t startIndex;
    uint32_t fineOffset;
    uint32_t cSpadsPerByte = 8;
    uint32_t coarseIndex;
    uint32_t fineIndex;
    uint8_t dataByte;
    uint8_t success = 0;


    *next = -1;

    startIndex = curr / cSpadsPerByte;
    fineOffset = curr % cSpadsPerByte;

    for (coarseIndex = startIndex; ((coarseIndex < size) && !success);
            coarseIndex++) {
        fineIndex = 0;
        dataByte = goodSpadArray[coarseIndex];

        if (coarseIndex == startIndex) {
            dataByte >>= fineOffset;
            fineIndex = fineOffset;
        }

        while (fineIndex < cSpadsPerByte) {
            if ((dataByte & 0x1) == 1) {
                success = 1;
                *next = coarseIndex * cSpadsPerByte + fineIndex;
                break;
            }
            dataByte >>= 1;
            fineIndex++;
        }
    }
}


uint8_t is_aperture(uint32_t spadIndex)
{
    uint32_t quadrant;
    uint8_t isAperture = 1;
    quadrant = spadIndex >> 6;
    if (refArrayQuadrants[quadrant] == REF_ARRAY_SPAD_0)
        isAperture = 0;

    return isAperture;
}


VL53L0_Error enable_spad_bit(uint8_t spadArray[], uint32_t size,
        uint32_t spadIndex)
{
    VL53L0_Error status = VL53L0_ERROR_NONE;
    uint32_t cSpadsPerByte = 8;
    uint32_t coarseIndex;
    uint32_t fineIndex;

    coarseIndex = spadIndex / cSpadsPerByte;
    fineIndex = spadIndex % cSpadsPerByte;
    if (coarseIndex >= size)
        status = VL53L0_ERROR_REF_SPAD_INIT;
    else
        spadArray[coarseIndex] |= (1 << fineIndex);

    return status;
}

VL53L0_Error count_enabled_spads(uint8_t spadArray[],
        uint32_t byteCount, uint32_t maxSpads,
        uint32_t *pTotalSpadsEnabled, uint8_t *pIsAperture)
{
    VL53L0_Error status = VL53L0_ERROR_NONE;
    uint32_t cSpadsPerByte = 8;
    uint32_t lastByte;
    uint32_t lastBit;
    uint32_t byteIndex = 0;
    uint32_t bitIndex = 0;
    uint8_t tempByte;
    uint8_t spadTypeIdentified = 0;


    lastByte = maxSpads / cSpadsPerByte;
    lastBit = maxSpads % cSpadsPerByte;

    
    if (lastByte >= byteCount)
        status = VL53L0_ERROR_REF_SPAD_INIT;

    *pTotalSpadsEnabled = 0;

    
    for (byteIndex = 0; byteIndex <= (lastByte - 1); byteIndex++) {
        tempByte = spadArray[byteIndex];

        for (bitIndex = 0; bitIndex <= cSpadsPerByte; bitIndex++) {
            if ((tempByte & 0x01) == 1) {
                (*pTotalSpadsEnabled)++;

                if (!spadTypeIdentified) {
                    *pIsAperture = 1;
                    if ((byteIndex < 2) && (bitIndex < 4))
                        *pIsAperture = 0;
                    spadTypeIdentified = 1;
                }
            }
            tempByte >>= 1;
        }
    }

    tempByte = spadArray[lastByte];

    for (bitIndex = 0; bitIndex <= lastBit; bitIndex++) {
        if ((tempByte & 0x01) == 1)
            (*pTotalSpadsEnabled)++;
    }

    return status;
}

VL53L0_Error set_ref_spad_map(VL53L0_DEV Dev, uint8_t *refSpadArray)
{
    VL53L0_Error status = VL53L0_WriteMulti(Dev,
            VL53L0_REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0,
            refSpadArray, 6);
    return status;
}

VL53L0_Error get_ref_spad_map(VL53L0_DEV Dev, uint8_t *refSpadArray)
{
    VL53L0_Error status = VL53L0_ReadMulti(Dev,
            VL53L0_REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0,
            refSpadArray,
            6);
    return status;
}

VL53L0_Error enable_ref_spads(VL53L0_DEV Dev,
        uint8_t apertureSpads,
        uint8_t goodSpadArray[],
        uint8_t spadArray[],
        uint32_t size,
        uint32_t start,
        uint32_t offset,
        uint32_t spadCount,
        uint32_t *lastSpad)
{
    VL53L0_Error status = VL53L0_ERROR_NONE;
    uint32_t index;
    uint32_t i;
    int32_t nextGoodSpad = offset;
    uint32_t currentSpad;
    uint8_t checkSpadArray[6];


    currentSpad = offset;
    for (index = 0; index < spadCount; index++) {
        get_next_good_spad(goodSpadArray, size, currentSpad,
                &nextGoodSpad);

        if (nextGoodSpad == -1) {
            status = VL53L0_ERROR_REF_SPAD_INIT;
            break;
        }

        
        if (is_aperture(start + nextGoodSpad) != apertureSpads) {
            status = VL53L0_ERROR_REF_SPAD_INIT;
            break;
        }
        currentSpad = (uint32_t)nextGoodSpad;
        enable_spad_bit(spadArray, size, currentSpad);
        currentSpad++;
    }
    *lastSpad = currentSpad;

    if (status == VL53L0_ERROR_NONE)
        status = set_ref_spad_map(Dev, spadArray);


    if (status == VL53L0_ERROR_NONE) {
        status = get_ref_spad_map(Dev, checkSpadArray);

        i = 0;

        
        while (i < size) {
            if (spadArray[i] != checkSpadArray[i]) {
                status = VL53L0_ERROR_REF_SPAD_INIT;
                break;
            }
            i++;
        }
    }
    return status;
}


VL53L0_Error perform_ref_signal_measurement(VL53L0_DEV Dev,
        uint16_t *refSignalRate)
{
    VL53L0_Error status = VL53L0_ERROR_NONE;
    VL53L0_RangingMeasurementData_t rangingMeasurementData;

    uint8_t SequenceConfig = 0;


    SequenceConfig = PALDevDataGet(Dev, SequenceConfig);

    if (status == VL53L0_ERROR_NONE)
        status = VL53L0_WrByte(Dev,
                VL53L0_REG_SYSTEM_SEQUENCE_CONFIG, 0xC0);

    if (status == VL53L0_ERROR_NONE)
        status = VL53L0_PerformSingleRangingMeasurement(Dev,
                &rangingMeasurementData);

    if (status == VL53L0_ERROR_NONE)
        status = VL53L0_WrByte(Dev, 0xFF, 0x01);

    if (status == VL53L0_ERROR_NONE)
        status = VL53L0_RdWord(Dev,
                VL53L0_REG_RESULT_PEAK_SIGNAL_RATE_REF,
                refSignalRate);

    if (status == VL53L0_ERROR_NONE)
        status = VL53L0_WrByte(Dev, 0xFF, 0x00);

    if (status == VL53L0_ERROR_NONE) {
        
        status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG,
                SequenceConfig);
        if (status == VL53L0_ERROR_NONE)
            PALDevDataSet(Dev, SequenceConfig, SequenceConfig);
    }

    return status;
}

VL53L0_Error VL53L0_perform_ref_spad_management(VL53L0_DEV Dev,
        uint32_t *refSpadCount,
        uint8_t *isApertureSpads)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t lastSpadArray[6];
    uint8_t startSelect = 0xB4;
    uint32_t minimumSpadCount = 3;
    uint32_t maxSpadCount = 44;
    uint32_t currentSpadIndex = 0;
    uint32_t lastSpadIndex = 0;
    int32_t nextGoodSpad = 0;
    uint16_t targetRefRate = 0x0A00; 
    uint16_t peakSignalRateRef;
    uint32_t needAptSpads = 0;
    uint32_t index = 0;
    uint32_t spadArraySize = 6;
    uint32_t signalRateDiff = 0;
    uint32_t lastSignalRateDiff = 0;
    uint8_t complete = 0;
    uint8_t VhvSettings = 0;
    uint8_t PhaseCal = 0;
    uint32_t refSpadCount_int = 0;
    uint8_t	 isApertureSpads_int = 0;



    targetRefRate = PALDevDataGet(Dev, targetRefRate);

    for (index = 0; index < spadArraySize; index++)
        Dev->Data.SpadData.RefSpadEnables[index] = 0;


    Status = VL53L0_WrByte(Dev, 0xFF, 0x01);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev, 0xFF, 0x00);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_GLOBAL_CONFIG_REF_EN_START_SELECT,
                startSelect);


    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_POWER_MANAGEMENT_GO1_POWER_FORCE, 0);

    
    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_perform_ref_calibration(Dev, &VhvSettings,
                &PhaseCal, 0);

    if (Status == VL53L0_ERROR_NONE) {
        
        currentSpadIndex = 0;
        lastSpadIndex = currentSpadIndex;
        needAptSpads = 0;
        Status = enable_ref_spads(Dev,
                needAptSpads,
                Dev->Data.SpadData.RefGoodSpadMap,
                Dev->Data.SpadData.RefSpadEnables,
                spadArraySize,
                startSelect,
                currentSpadIndex,
                minimumSpadCount,
                &lastSpadIndex);
    }

    if (Status == VL53L0_ERROR_NONE) {
        currentSpadIndex = lastSpadIndex;

        Status = perform_ref_signal_measurement(Dev,
                &peakSignalRateRef);
        if ((Status == VL53L0_ERROR_NONE) &&
                (peakSignalRateRef > targetRefRate)) {

            for (index = 0; index < spadArraySize; index++)
                Dev->Data.SpadData.RefSpadEnables[index] = 0;


            
            while ((is_aperture(startSelect + currentSpadIndex)
                        == 0) && (currentSpadIndex < maxSpadCount)) {
                currentSpadIndex++;
            }

            needAptSpads = 1;

            Status = enable_ref_spads(Dev,
                    needAptSpads,
                    Dev->Data.SpadData.RefGoodSpadMap,
                    Dev->Data.SpadData.RefSpadEnables,
                    spadArraySize,
                    startSelect,
                    currentSpadIndex,
                    minimumSpadCount,
                    &lastSpadIndex);

            if (Status == VL53L0_ERROR_NONE) {
                currentSpadIndex = lastSpadIndex;
                Status = perform_ref_signal_measurement(Dev,
                        &peakSignalRateRef);

                if ((Status == VL53L0_ERROR_NONE) &&
                        (peakSignalRateRef > targetRefRate)) {
                    isApertureSpads_int = 1;
                    refSpadCount_int = minimumSpadCount;
                }
            }
        } else {
            needAptSpads = 0;
        }
    }

    if ((Status == VL53L0_ERROR_NONE) &&
            (peakSignalRateRef < targetRefRate)) {
        isApertureSpads_int = needAptSpads;
        refSpadCount_int	= minimumSpadCount;

        memcpy(lastSpadArray, Dev->Data.SpadData.RefSpadEnables,
                spadArraySize);
        lastSignalRateDiff = abs(peakSignalRateRef -
                targetRefRate);
        complete = 0;

        while (!complete) {
            get_next_good_spad(
                    Dev->Data.SpadData.RefGoodSpadMap,
                    spadArraySize, currentSpadIndex,
                    &nextGoodSpad);

            if (nextGoodSpad == -1) {
                Status = VL53L0_ERROR_REF_SPAD_INIT;
                break;
            }

            

            if (is_aperture((uint32_t)startSelect + nextGoodSpad) !=
                    needAptSpads) {
                
                complete = 1;
                break;
            }

            (refSpadCount_int)++;

            currentSpadIndex = nextGoodSpad;
            Status = enable_spad_bit(
                    Dev->Data.SpadData.RefSpadEnables,
                    spadArraySize, currentSpadIndex);

            if (Status == VL53L0_ERROR_NONE) {
                currentSpadIndex++;
                Status = set_ref_spad_map(Dev,
                        Dev->Data.SpadData.RefSpadEnables);
            }

            if (Status != VL53L0_ERROR_NONE)
                break;

            Status = perform_ref_signal_measurement(Dev,
                    &peakSignalRateRef);

            if (Status != VL53L0_ERROR_NONE)
                break;

            signalRateDiff = abs(peakSignalRateRef - targetRefRate);

            if (peakSignalRateRef > targetRefRate) {
                if (signalRateDiff > lastSignalRateDiff) {
                    Status = set_ref_spad_map(Dev,
                            lastSpadArray);
                    memcpy(
                            Dev->Data.SpadData.RefSpadEnables,
                            lastSpadArray, spadArraySize);

                    (refSpadCount_int)--;
                }
                complete = 1;
            } else {
                
                lastSignalRateDiff = signalRateDiff;
                memcpy(lastSpadArray,
                        Dev->Data.SpadData.RefSpadEnables,
                        spadArraySize);
            }

        } 
    }

    if (Status == VL53L0_ERROR_NONE) {
        *refSpadCount = refSpadCount_int;
        *isApertureSpads = isApertureSpads_int;

        VL53L0_SETDEVICESPECIFICPARAMETER(Dev, RefSpadsInitialised, 1);
        VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadCount, (uint8_t)(*refSpadCount));
        VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadType, *isApertureSpads);
    }

    return Status;
}

VL53L0_Error VL53L0_set_reference_spads(VL53L0_DEV Dev,
        uint32_t count, uint8_t isApertureSpads)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint32_t currentSpadIndex = 0;
    uint8_t startSelect = 0xB4;
    uint32_t spadArraySize = 6;
    uint32_t maxSpadCount = 44;
    uint32_t lastSpadIndex;
    uint32_t index;


    Status = VL53L0_WrByte(Dev, 0xFF, 0x01);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev, 0xFF, 0x00);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev,
                VL53L0_REG_GLOBAL_CONFIG_REF_EN_START_SELECT,
                startSelect);

    for (index = 0; index < spadArraySize; index++)
        Dev->Data.SpadData.RefSpadEnables[index] = 0;

    if (isApertureSpads) {
        
        while ((is_aperture(startSelect + currentSpadIndex) == 0) &&
                (currentSpadIndex < maxSpadCount)) {
            currentSpadIndex++;
        }
    }
    Status = enable_ref_spads(Dev,
            isApertureSpads,
            Dev->Data.SpadData.RefGoodSpadMap,
            Dev->Data.SpadData.RefSpadEnables,
            spadArraySize,
            startSelect,
            currentSpadIndex,
            count,
            &lastSpadIndex);

    if (Status == VL53L0_ERROR_NONE) {
        VL53L0_SETDEVICESPECIFICPARAMETER(Dev, RefSpadsInitialised, 1);
        VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadCount, (uint8_t)(count));
        VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadType, isApertureSpads);
    }

    return Status;
}

VL53L0_Error VL53L0_get_reference_spads(VL53L0_DEV Dev,
        uint32_t *pSpadCount, uint8_t *pIsApertureSpads)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t refSpadsInitialised;
    uint8_t refSpadArray[6];
    uint32_t cMaxSpadCount = 44;
    uint32_t cSpadArraySize = 6;
    uint32_t spadsEnabled;
    uint8_t isApertureSpads = 0;

    refSpadsInitialised = VL53L0_GETDEVICESPECIFICPARAMETER(Dev,
            RefSpadsInitialised);

    if (refSpadsInitialised == 1) {

        *pSpadCount   = (uint32_t)VL53L0_GETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadCount);
        *pIsApertureSpads = VL53L0_GETDEVICESPECIFICPARAMETER(Dev,
                ReferenceSpadType);
    } else {

        
        Status = get_ref_spad_map(Dev, refSpadArray);

        if (Status == VL53L0_ERROR_NONE) {
            Status = count_enabled_spads(refSpadArray,
                    cSpadArraySize,
                    cMaxSpadCount,
                    &spadsEnabled,
                    &isApertureSpads);

            if (Status == VL53L0_ERROR_NONE) {

                *pSpadCount = spadsEnabled;
                *pIsApertureSpads = isApertureSpads;

                VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                        RefSpadsInitialised, 1);
                VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                        ReferenceSpadCount,
                        (uint8_t)spadsEnabled);
                VL53L0_SETDEVICESPECIFICPARAMETER(Dev,
                        ReferenceSpadType, isApertureSpads);
            }
        }
    }

    return Status;
}


VL53L0_Error VL53L0_perform_single_ref_calibration(VL53L0_DEV Dev,
        uint8_t vhv_init_byte)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSRANGE_START,
                VL53L0_REG_SYSRANGE_MODE_START_STOP |
                vhv_init_byte);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_measurement_poll_for_completion(Dev);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_ClearInterruptMask(Dev, 0);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSRANGE_START, 0x00);

    return Status;
}


VL53L0_Error VL53L0_ref_calibration_io(VL53L0_DEV Dev, uint8_t read_not_write,
        uint8_t VhvSettings, uint8_t PhaseCal,
        uint8_t *pVhvSettings, uint8_t *pPhaseCal,
        const uint8_t vhv_enable, const uint8_t phase_enable)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t PhaseCalint = 0;

    
    Status |= VL53L0_WrByte(Dev, 0xFF, 0x01);
    Status |= VL53L0_WrByte(Dev, 0x00, 0x00);
    Status |= VL53L0_WrByte(Dev, 0xFF, 0x00);

    if (read_not_write) {
        if (vhv_enable)
            Status |= VL53L0_RdByte(Dev, 0xCB, pVhvSettings);
        if (phase_enable)
            Status |= VL53L0_RdByte(Dev, 0xEE, &PhaseCalint);
    } else {
        if (vhv_enable)
            Status |= VL53L0_WrByte(Dev, 0xCB, VhvSettings);
        if (phase_enable)
            Status |= VL53L0_UpdateByte(Dev, 0xEE, 0x80, PhaseCal);
    }

    Status |= VL53L0_WrByte(Dev, 0xFF, 0x01);
    Status |= VL53L0_WrByte(Dev, 0x00, 0x01);
    Status |= VL53L0_WrByte(Dev, 0xFF, 0x00);

    *pPhaseCal = (uint8_t)(PhaseCalint&0xEF);

    return Status;
}


VL53L0_Error VL53L0_perform_vhv_calibration(VL53L0_DEV Dev,
        uint8_t *pVhvSettings, const uint8_t get_data_enable,
        const uint8_t restore_config)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t SequenceConfig = 0;
    uint8_t VhvSettings = 0;
    uint8_t PhaseCal = 0;
    uint8_t PhaseCalInt = 0;


    if (restore_config)
        SequenceConfig = PALDevDataGet(Dev, SequenceConfig);

    
    Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG, 0x01);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_perform_single_ref_calibration(Dev, 0x40);

    
    if ((Status == VL53L0_ERROR_NONE) && (get_data_enable == 1)) {
        Status = VL53L0_ref_calibration_io(Dev, 1,
                VhvSettings, PhaseCal, 
                pVhvSettings, &PhaseCalInt,
                1, 0);
    } else
        *pVhvSettings = 0;


    if ((Status == VL53L0_ERROR_NONE) && restore_config) {
        
        Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG,
                SequenceConfig);
        if (Status == VL53L0_ERROR_NONE)
            PALDevDataSet(Dev, SequenceConfig, SequenceConfig);

    }

    return Status;
}

VL53L0_Error VL53L0_perform_phase_calibration(VL53L0_DEV Dev,
        uint8_t *pPhaseCal, const uint8_t get_data_enable,
        const uint8_t restore_config)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t SequenceConfig = 0;
    uint8_t VhvSettings = 0;
    uint8_t PhaseCal = 0;
    uint8_t VhvSettingsint;


    if (restore_config)
        SequenceConfig = PALDevDataGet(Dev, SequenceConfig);

    
    Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG, 0x02);

    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_perform_single_ref_calibration(Dev, 0x0);

    
    if ((Status == VL53L0_ERROR_NONE) && (get_data_enable == 1)) {
        Status = VL53L0_ref_calibration_io(Dev, 1,
                VhvSettings, PhaseCal, 
                &VhvSettingsint, pPhaseCal,
                0, 1);
    } else
        *pPhaseCal = 0;


    if ((Status == VL53L0_ERROR_NONE) && restore_config) {
        
        Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG,
                SequenceConfig);
        if (Status == VL53L0_ERROR_NONE)
            PALDevDataSet(Dev, SequenceConfig, SequenceConfig);

    }

    return Status;
}

VL53L0_Error VL53L0_perform_ref_calibration(VL53L0_DEV Dev,
        uint8_t *pVhvSettings, uint8_t *pPhaseCal, uint8_t get_data_enable)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t SequenceConfig = 0;


    SequenceConfig = PALDevDataGet(Dev, SequenceConfig);

    Status = VL53L0_perform_vhv_calibration(
            Dev, pVhvSettings, get_data_enable, 0);


    if (Status == VL53L0_ERROR_NONE)
        Status = VL53L0_perform_phase_calibration(
                Dev, pPhaseCal, get_data_enable, 0);


    if (Status == VL53L0_ERROR_NONE) {
        
        Status = VL53L0_WrByte(Dev, VL53L0_REG_SYSTEM_SEQUENCE_CONFIG,
                SequenceConfig);
        if (Status == VL53L0_ERROR_NONE)
            PALDevDataSet(Dev, SequenceConfig, SequenceConfig);

    }

    return Status;
}

VL53L0_Error VL53L0_set_ref_calibration(VL53L0_DEV Dev,
        uint8_t VhvSettings, uint8_t PhaseCal)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t pVhvSettings;
    uint8_t pPhaseCal;

    Status = VL53L0_ref_calibration_io(Dev, 0,
            VhvSettings, PhaseCal,
            &pVhvSettings, &pPhaseCal,
            1, 1);

    return Status;
}

VL53L0_Error VL53L0_get_ref_calibration(VL53L0_DEV Dev,
        uint8_t *pVhvSettings, uint8_t *pPhaseCal)
{
    VL53L0_Error Status = VL53L0_ERROR_NONE;
    uint8_t VhvSettings = 0;
    uint8_t PhaseCal = 0;

    Status = VL53L0_ref_calibration_io(Dev, 1,
            VhvSettings, PhaseCal,
            pVhvSettings, pPhaseCal,
            1, 1);

    return Status;
}
