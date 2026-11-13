import os
import json

workspace = "D:/Desktop/淘晶驰/Breath"

include_dirs = [
    f"{workspace}/Core/Inc",
    f"{workspace}/Screen",
    f"{workspace}/Bluetooth",
    f"{workspace}/Audio",
    f"{workspace}/Stimulator",
    f"{workspace}/BreathSensor",
    f"{workspace}/Storage",
    f"{workspace}/MainTask",
    f"{workspace}/Algorithm/Inc",
    f"{workspace}/FATFS/Target",
    f"{workspace}/FATFS/App",
    f"{workspace}/Drivers/STM32F4xx_HAL_Driver/Inc",
    f"{workspace}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy",
    f"{workspace}/Middlewares/Third_Party/FreeRTOS/Source/include",
    f"{workspace}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS",
    f"{workspace}/Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F",
    f"{workspace}/Middlewares/Third_Party/FatFs/src",
    f"{workspace}/Drivers/CMSIS/Device/ST/STM32F4xx/Include",
    f"{workspace}/Drivers/CMSIS/Include",
    f"{workspace}/Middlewares/ST/ARM/DSP/Inc"
]

defines = [
    "-DUSE_HAL_DRIVER",
    "-DSTM32F405xx",
    "-DARM_MATH_CM4",
    "-DHSE_VALUE=8000000",
    "-D__CC_ARM",
    "-D__arm__",
    "-D__align(x)=",
    "-D__ALIGNOF__(x)=",
    "-D__alignof__(x)=",
    "-D__asm(x)=",
    "-D__forceinline=inline",
    "-D__restrict=",
    "-D__global_reg(n)=",
    "-D__inline=inline",
    "-D__int64=long long",
    "-D__INTADDR__(expr)=0",
    "-D__irq=",
    "-D__packed=",
    "-D__pure=",
    "-D__smc(n)=",
    "-D__svc(n)=",
    "-D__svc_indirect(n)=",
    "-D__svc_indirect_r7(n)=",
    "-D__value_in_regs=",
    "-D__weak=__attribute__((weak))",
    "-D__writeonly=",
    "-D__declspec(x)=",
    "-D__attribute__(x)=",
    "-D__nonnull__(x)=",
    "-D__register=",
    "-D__breakpoint(x)=",
    "-D__clrex()=",
    "-D__clz(x)=0U",
    "-D__disable_fiq()=",
    "-D__disable_irq()=",
    "-D__dmb(x)=",
    "-D__dsb(x)=",
    "-D__enable_fiq()=",
    "-D__enable_irq()=",
    "-D__isb(x)=",
    "-D__ldrex(x)=0U",
    "-D__nop()=",
    "-D__rbit(x)=0U",
    "-D__rev(x)=0U",
    "-D__strex(x,y)=0U",
    "-D__wfe()=",
    "-D__wfi()="
]

flags = [
    "clang",
    "-c",
    "-std=c99",
    "--target=arm-none-eabi",
    "-mcpu=cortex-m4",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
    "-isystem", "D:/ARM/ARMCC/include",
    "-isystem", "D:/ARM/ARMCC/include/rw"
] + [f"-I{inc}" for inc in include_dirs] + defines

c_files = []
for root, dirs, files in os.walk(r"D:\Desktop\淘晶驰\Breath"):
    if "MDK-ARM" in root or ".git" in root or ".vscode" in root:
        continue
    for f in files:
        if f.endswith(".c"):
            full_path = os.path.join(root, f).replace("\\", "/")
            c_files.append(full_path)

compile_commands = []
for c_file in sorted(c_files):
    compile_commands.append({
        "directory": workspace,
        "file": c_file,
        "arguments": flags + ["-o", f"{c_file}.o", c_file]
    })

out_path = os.path.join(r"D:\Desktop\淘晶驰\Breath", "compile_commands.json")
with open(out_path, "w", encoding="utf-8") as fp:
    json.dump(compile_commands, fp, indent=2, ensure_ascii=False)

print(f"Generated {out_path} with {len(compile_commands)} files.")
