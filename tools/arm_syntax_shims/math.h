#ifndef CAT1_ARM_SYNTAX_SHIM_MATH_H
#define CAT1_ARM_SYNTAX_SHIM_MATH_H

/* Host SDK math.h exposes target-incompatible _Float16 declarations.
 * This header is used only by tools/arm_gcc_syntax_check.sh. */
extern float sqrtf(float value);
extern float powf(float base, float exponent);

#endif
