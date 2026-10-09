################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/OLED.c \
../Core/Src/admin.c \
../Core/Src/attendance.c \
../Core/Src/attlog.c \
../Core/Src/card_db.c \
../Core/Src/flash.c \
../Core/Src/main.c \
../Core/Src/rc522.c \
../Core/Src/rfid_app.c \
../Core/Src/rtc_clock.c \
../Core/Src/stm32f1xx_hal_msp.c \
../Core/Src/stm32f1xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f1xx.c \
../Core/Src/uart_cmd.c 

OBJS += \
./Core/Src/OLED.o \
./Core/Src/admin.o \
./Core/Src/attendance.o \
./Core/Src/attlog.o \
./Core/Src/card_db.o \
./Core/Src/flash.o \
./Core/Src/main.o \
./Core/Src/rc522.o \
./Core/Src/rfid_app.o \
./Core/Src/rtc_clock.o \
./Core/Src/stm32f1xx_hal_msp.o \
./Core/Src/stm32f1xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f1xx.o \
./Core/Src/uart_cmd.o 

C_DEPS += \
./Core/Src/OLED.d \
./Core/Src/admin.d \
./Core/Src/attendance.d \
./Core/Src/attlog.d \
./Core/Src/card_db.d \
./Core/Src/flash.d \
./Core/Src/main.d \
./Core/Src/rc522.d \
./Core/Src/rfid_app.d \
./Core/Src/rtc_clock.d \
./Core/Src/stm32f1xx_hal_msp.d \
./Core/Src/stm32f1xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f1xx.d \
./Core/Src/uart_cmd.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"D:/RFID/build/Debug" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/OLED.cyclo ./Core/Src/OLED.d ./Core/Src/OLED.o ./Core/Src/OLED.su ./Core/Src/admin.cyclo ./Core/Src/admin.d ./Core/Src/admin.o ./Core/Src/admin.su ./Core/Src/attendance.cyclo ./Core/Src/attendance.d ./Core/Src/attendance.o ./Core/Src/attendance.su ./Core/Src/attlog.cyclo ./Core/Src/attlog.d ./Core/Src/attlog.o ./Core/Src/attlog.su ./Core/Src/card_db.cyclo ./Core/Src/card_db.d ./Core/Src/card_db.o ./Core/Src/card_db.su ./Core/Src/flash.cyclo ./Core/Src/flash.d ./Core/Src/flash.o ./Core/Src/flash.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/rc522.cyclo ./Core/Src/rc522.d ./Core/Src/rc522.o ./Core/Src/rc522.su ./Core/Src/rfid_app.cyclo ./Core/Src/rfid_app.d ./Core/Src/rfid_app.o ./Core/Src/rfid_app.su ./Core/Src/rtc_clock.cyclo ./Core/Src/rtc_clock.d ./Core/Src/rtc_clock.o ./Core/Src/rtc_clock.su ./Core/Src/stm32f1xx_hal_msp.cyclo ./Core/Src/stm32f1xx_hal_msp.d ./Core/Src/stm32f1xx_hal_msp.o ./Core/Src/stm32f1xx_hal_msp.su ./Core/Src/stm32f1xx_it.cyclo ./Core/Src/stm32f1xx_it.d ./Core/Src/stm32f1xx_it.o ./Core/Src/stm32f1xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f1xx.cyclo ./Core/Src/system_stm32f1xx.d ./Core/Src/system_stm32f1xx.o ./Core/Src/system_stm32f1xx.su ./Core/Src/uart_cmd.cyclo ./Core/Src/uart_cmd.d ./Core/Src/uart_cmd.o ./Core/Src/uart_cmd.su

.PHONY: clean-Core-2f-Src

