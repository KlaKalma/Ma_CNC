# Active le FPU du Cortex-M4 : par defaut, le flottant serait calcule par logiciel.
Import("env")

options = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]
env.Append(CCFLAGS=options, ASFLAGS=options, LINKFLAGS=options)
