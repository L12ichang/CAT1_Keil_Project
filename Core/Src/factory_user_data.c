/*************************************************************
程序功能：工厂参数
开发环境：keil 5.37
芯片型号：STM32F103CBT6/HK32F103CCT6A
开发人员：王道军
单位名称：广东东菱电源科技有限公司
编辑日期：2024.7.1
*************************************************************/

#include "factory_user_data.h"
#include "sys_data.h"
#include "sys_pwm.h"
#include "sys_calibration_service.h"
 u8  factory_user_buff[128];

u16 SET_OUTCUR_temp;
u16 HWMAX_OUTCUR_temp;
u16 OUTPUT_CUR_SENSOR_temp;
u16 OP_PWM_OFFSET_temp;
u16 BOUND_OUTPUT_VOLTAGE_01V_temp;

static void factory_user_set_u16be(u8 *buffer, u16 offset, u16 value)
{
    buffer[offset] = (u8)(value >> 8U);
    buffer[offset + 1U] = (u8)value;
}

static u16 factory_user_get_u16be(const u8 *buffer, u16 offset)
{
    return (u16)(((u16)buffer[offset] << 8U) | buffer[offset + 1U]);
}

static void factory_user_sync_product_fields(void)
{
    factory_user_buff[0x05] = MID;
    factory_user_set_u16be(factory_user_buff, 0x08U, BOUND_OUTPUT_VOLTAGE_01V);
    factory_user_set_u16be(factory_user_buff, 0x10U, SET_OUTCUR);
    factory_user_set_u16be(factory_user_buff, 0x12U, HWMAX_OUTCUR);
    factory_user_set_u16be(factory_user_buff, 0x14U, OUTPUT_CUR_SENSOR);
    memcpy(sys_data.fa_Parambuf, factory_user_buff, sizeof(factory_user_buff));
}

void fac_128_data_default(void)
{

    if( MID==0xff||MID==0x00)
    {
        MID=FACTORY_DEFAULT_MID;
    }
    if (BOUND_OUTPUT_VOLTAGE_01V < SYS_PRODUCT_PROFILE_CURRENT_MIN_VOLTAGE_01V ||
        BOUND_OUTPUT_VOLTAGE_01V > SYS_PRODUCT_PROFILE_CURRENT_MAX_VOLTAGE_01V ||
        BOUND_OUTPUT_VOLTAGE_01V == SYS_PRODUCT_PROFILE_SPECIAL_TEST_VOLTAGE_01V)
    {
        BOUND_OUTPUT_VOLTAGE_01V = FACTORY_DEFAULT_BOUND_VOLTAGE_01V;
    }
    if( OUTPUT_CUR_SENSOR==0xffff||OUTPUT_CUR_SENSOR==0x00)
    {
      OUTPUT_CUR_SENSOR=FACTORY_DEFAULT_OUTPUT_CUR_SENSOR_MOHM;
    }
    if(OP_PWM_OFFSET==0xffff||OP_PWM_OFFSET==0x00)
    {
    
      OP_PWM_OFFSET=FACTORY_DEFAULT_PWM_OFFSET_PERMILLE;
    
    }
      if(SET_OUTCUR==0xffff||SET_OUTCUR==0x00||SET_OUTCUR>FACTORY_OUTCUR_MAX_MA)
    {
    
      SET_OUTCUR=FACTORY_DEFAULT_SET_OUTCUR_MA;
    
    }
      if(HWMAX_OUTCUR==0xffff||HWMAX_OUTCUR==0x00||HWMAX_OUTCUR>FACTORY_OUTCUR_MAX_MA)
    {
    
      HWMAX_OUTCUR=FACTORY_DEFAULT_HWMAX_OUTCUR_MA;
    
    }
    if( INNRE_TEMP_PRO_EN==0xff)
    {
      INNRE_TEMP_PRO_EN =FACTORY_DEFAULT_TEMP_PROTECT_ENABLE;
    }
    if( INNRE_TEMP_PRO==-1)
    {
      INNRE_TEMP_PRO =FACTORY_DEFAULT_TEMP_PROTECT_C;
    }
     if(CX==0xff||CX==0x00 ) 
    {
        CX=FACTORY_DEFAULT_CX_CENTI_UF;
    }
    if(SCHEDULE_SIZE>7)
    {
       SCHEDULE_SIZE=7;//默认单日最大调光动作数
    }
    if (HWMAX_OUTCUR > SYS_PRODUCT_PROFILE_CURRENT_HARDWARE_MAX_MA)
    {
        HWMAX_OUTCUR = FACTORY_DEFAULT_HWMAX_OUTCUR_MA;
    }
    if (SET_OUTCUR > HWMAX_OUTCUR)
    {
        SET_OUTCUR = FACTORY_DEFAULT_SET_OUTCUR_MA;
        if (SET_OUTCUR > HWMAX_OUTCUR)
        {
            SET_OUTCUR = HWMAX_OUTCUR;
        }
    }
    factory_user_sync_product_fields();

}



/************************************
功能描述：加载flash里的工厂参数和用户参数到 factory_user_buff
输入参数：无
输出返回：无
*************************************/
void factory_user_load_data(void)
{      //两字节以上要大小端转换
       memcpy(factory_user_buff, sys_data.fa_Parambuf, 128);
       u16h(BOUND_OUTPUT_VOLTAGE_01V_temp) = (*((u8*)(factory_user_buff+0x08)));
       u16l(BOUND_OUTPUT_VOLTAGE_01V_temp) = (*((u8*)(factory_user_buff+0x09)));
       u16h(SET_OUTCUR_temp)  =        (*((u8*)(factory_user_buff+0x10))); 
       u16l(SET_OUTCUR_temp)  =        (*((u8*)(factory_user_buff+0x11))); 
       u16h(HWMAX_OUTCUR_temp)=        (*((u8*)(factory_user_buff+0x12)));
       u16l(HWMAX_OUTCUR_temp)=        (*((u8*)(factory_user_buff+0x13)));
       u16h(OUTPUT_CUR_SENSOR_temp)=   (*((u8*)(factory_user_buff+0x14)));
       u16l(OUTPUT_CUR_SENSOR_temp)=   (*((u8*)(factory_user_buff+0x15)));
       u16h(OP_PWM_OFFSET_temp)    =   (*((u8*)(factory_user_buff+0x16)));
       u16l(OP_PWM_OFFSET_temp)    =   (*((u8*)(factory_user_buff+0x17)));
   
       fac_128_data_default();//检查参数合法性,不合法设为默认值

 
    

     
#ifdef DEBUG_PRINTF
    printf("Protocol_version=%d\n", (u16)Protocol_version);
    printf("色温=%d\n" , (u16)Config3.TintingTemperatureValid);

    
    printf("kx+b使能= %d\n", (u16)Formula_k_H.enable);
    printf("k= %d\n", (u16)kx_b_k);
    printf("b= %d\n", (u16)kx_b_b);

    printf("寿命预警= %d\n", (u16)ExtFuncEnable2.LIFE_ALARM);
    printf("寿命预警时间= %d 0.5kh\n", (u16)LifeAlarmTime);
    printf("报警时间= %d 分\n", (u16)LifeAlarmPeriod);
    printf("报警次数= %d\n", (u16)LifeAlarmTimes);

#endif



    


}

sys_product_current_validation_en factory_user_validate_runtime_current(
    u16 bound_voltage_01v,
    u32 configured_current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    (void)bound_voltage_01v;
    if (profile == NULL ||
        sys_product_profile_runtime_matches(MID, OUTPUT_CUR_SENSOR,
                                            HWMAX_OUTCUR) != BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    if (configured_current_ma == 0U)
    {
        return SYS_PRODUCT_CURRENT_ZERO;
    }
    if (configured_current_ma > HWMAX_OUTCUR)
    {
        return SYS_PRODUCT_CURRENT_HW_MAX;
    }
    return SYS_PRODUCT_CURRENT_VALID;
}

sys_product_current_validation_en factory_user_validate_candidate(
    const u8 *factory_buffer)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 configured_current_ma;
    u16 hw_max_current_ma;
    u16 rs3_mohm;

    if (sys_calibration_service_is_output_authorized() == BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE;
    }
    if (factory_buffer == NULL ||
        sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    configured_current_ma = factory_user_get_u16be(factory_buffer, 0x10U);
    hw_max_current_ma = factory_user_get_u16be(factory_buffer, 0x12U);
    rs3_mohm = factory_user_get_u16be(factory_buffer, 0x14U);
    if (factory_buffer[0x05] != profile->mid ||
        rs3_mohm != profile->rs3_mohm)
    {
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    if (hw_max_current_ma == 0U ||
        hw_max_current_ma > profile->hw_max_current_ma)
    {
        return SYS_PRODUCT_CURRENT_HW_MAX;
    }
    if (configured_current_ma == 0U)
    {
        return SYS_PRODUCT_CURRENT_ZERO;
    }
    return (configured_current_ma <= hw_max_current_ma) ?
           SYS_PRODUCT_CURRENT_VALID : SYS_PRODUCT_CURRENT_HW_MAX;
}

sys_product_current_validation_en factory_user_set_hwmax_current(
    u32 hwmax_current_ma)
{
    const sys_product_profile_st *profile = sys_product_profile_current();
    u16 previous_hwmax;

    if (sys_calibration_service_is_output_authorized() == BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE;
    }
    if (sys_product_profile_is_complete(profile) != BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    if (hwmax_current_ma == 0U || hwmax_current_ma > profile->hw_max_current_ma ||
        SET_OUTCUR > hwmax_current_ma)
    {
        return SYS_PRODUCT_CURRENT_HW_MAX;
    }
    previous_hwmax = HWMAX_OUTCUR;
    HWMAX_OUTCUR = (u16)hwmax_current_ma;
    factory_user_sync_product_fields();
    if (sys_data_store_checked() != BOOL_TRUE)
    {
        HWMAX_OUTCUR = previous_hwmax;
        factory_user_sync_product_fields();
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    sys_pwm_reload();
    return SYS_PRODUCT_CURRENT_VALID;
}

sys_product_current_validation_en factory_user_set_runtime_current(
    u32 configured_current_ma)
{
    sys_product_current_validation_en result;
    u16 previous_set;
    if (sys_calibration_service_is_output_authorized() == BOOL_TRUE)
    {
        return SYS_PRODUCT_CURRENT_CALIBRATION_ACTIVE;
    }
    result =
        factory_user_validate_runtime_current(BOUND_OUTPUT_VOLTAGE_01V,
                                              configured_current_ma);
    if (result != SYS_PRODUCT_CURRENT_VALID)
    {
        return result;
    }
    previous_set = SET_OUTCUR;
    SET_OUTCUR = (u16)configured_current_ma;
    factory_user_sync_product_fields();
    if (sys_data_store_checked() != BOOL_TRUE)
    {
        SET_OUTCUR = previous_set;
        factory_user_sync_product_fields();
        return SYS_PRODUCT_CURRENT_PROFILE_INCOMPLETE;
    }
    sys_pwm_reload();
    return SYS_PRODUCT_CURRENT_VALID;
}

u16 factory_user_get_bound_output_voltage_01v(void)
{
    return BOUND_OUTPUT_VOLTAGE_01V;
}


