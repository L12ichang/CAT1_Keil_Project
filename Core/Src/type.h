#ifndef TYPE_H
#define TYPE_H

typedef unsigned char        boolt; 
typedef unsigned char        u8; 
typedef signed char          s8; 
typedef unsigned short int  u16; 
typedef signed short int    s16; 
typedef unsigned   int       u32; 
typedef signed  int          s32; 
typedef signed long long    s64;
typedef unsigned long long  u64;

typedef unsigned char         u1t;
typedef unsigned short int    u2t;
typedef signed char           s1t;
typedef signed short int      s2t;




typedef u8 uint8_t;
typedef u16 uint16_t;

typedef unsigned char     uint8;
typedef unsigned short    uint16;
typedef unsigned long     uint32;
typedef   signed char     int8;
typedef   signed short    int16;
typedef   signed long     int32;





typedef enum
{
    BOOL_FALSE=0,
    BOOL_TRUE,
}boolean_en;


//#define ENABLE_INTERRUPT()    NVIC_EnableIRQ;    
//#define DISABLE_INTERRUPT()   NVIC_DisableIRQ;       

#define DISABLE_INTERRUPT()         __disable_irq();
#define ENABLE_INTERRUPT()          __enable_irq();


/* 设置大端模式.一般8051内核是大端模式，其他是小端模式。如果是8051内核就开放这个宏定义 */

//#define BIG_ENDIAN     
#if (!defined( BIG_ENDIAN))  
//这是小端模式，arm使用。 
/* 2 bytes to 1byte */
#define  u16h(x)  (*((u8*)(&x)+1))
#define  u16l(x)  (*((u8*)(&x)+0))
/* 4 bytes to 2bytes */
#define  u32h(x) (*((u16*)(&x)+1))//以u2t为偏移量单位
#define  u32l(x) (*((u16*)(&x)+0))
/* 4 bytes to 1bytes */
#define  u32hh(x) (*((u8*)(&x)+3))
#define  u32hl(x) (*((u8*)(&x)+2))
#define  u32lh(x) (*((u8*)(&x)+1))
#define  u32ll(x) (*((u8*)(&x)+0))
/* 8 bytes to 1bytes */
#define  u64byte7(x) (*((u8*)(&x)+7)) /* 最高字节 */
#define  u64byte6(x) (*((u8*)(&x)+6))
#define  u64byte5(x) (*((u8*)(&x)+5))
#define  u64byte4(x) (*((u8*)(&x)+4))
#define  u64byte3(x) (*((u8*)(&x)+3))
#define  u64byte2(x) (*((u8*)(&x)+2))
#define  u64byte1(x) (*((u8*)(&x)+1))
#define  u64byte0(x) (*((u8*)(&x)+0))
/* 8 bytes to 4bytes */
#define  u64h(x) (*((u32*)((unsigned char*)(&x)+4)))   //以字节为偏移量单位
#define  u64l(x) (*((u32*)((unsigned char*)(&x)+0)))
//两种表达方式都是可以的
//#define  u64h(x) (*((u32*)(&x)+1))     //以u4t为偏移量单位
//#define  u64l(x) (*((u32*)(&x)+0))    
#else
/* 2 bytes to 1byte */
#define  u16h(x)  (*((u8*)(&x)+0))
#define  u16l(x)  (*((u8*)(&x)+1))
/* 4 bytes to 2bytes */
#define  u32h(x) (*((u16*)(&x)+0))//以u2t为偏移量单位
#define  u32l(x) (*((u16*)(&x)+1))
/* 4 bytes to 1bytes */
#define  u32hh(x) (*((u8*)(&x)+0))
#define  u32hl(x) (*((u8*)(&x)+1))
#define  u32lh(x) (*((u8*)(&x)+2))
#define  u32ll(x) (*((u8*)(&x)+3))
/* 8 bytes to 1bytes */
#define  u64byte7(x) (*((u8*)(&x)+0)) /* 最高字节 */
#define  u64byte6(x) (*((u8*)(&x)+1))
#define  u64byte5(x) (*((u8*)(&x)+2))
#define  u64byte4(x) (*((u8*)(&x)+3))
#define  u64byte3(x) (*((u8*)(&x)+4))
#define  u64byte2(x) (*((u8*)(&x)+5))
#define  u64byte1(x) (*((u8*)(&x)+6))
#define  u64byte0(x) (*((u8*)(&x)+7))
/* 8 bytes to 4bytes */
#define  u64h(x) (*((u32*)((unsigned char*)(&x)+0)))   //以字节为偏移量单位
#define  u64l(x) (*((u32*)((unsigned char*)(&x)+4)))
//两种表达方式都是可以的
//#define  u64h(x) (*((u32*)(&x)+0))     //以u4t为偏移量单位
//#define  u64l(x) (*((u32*)(&x)+1))    

#endif



#ifndef NULL
#define NULL    ((void *)0)
#endif


#ifndef FALSE
#define FALSE         (0)
#endif

#ifndef TRUE
#define TRUE          (1)
#endif
#ifndef false
#define false         (0)
#endif

#ifndef true
#define true          (1)
#endif

#ifndef HIGH_LEVEL
#define HIGH_LEVEL    (1)
#endif

#ifndef LOW_LEVEL
#define LOW_LEVEL     (0)
#endif

#ifndef BYTE
#define BYTE      u8
#endif

#ifndef WORD
#define WORD      u16
#endif

#ifndef DWORD
#define DWORD     u32
#endif



#define  ArrSize( a )  ( sizeof( (a) ) / sizeof( (a[0]) ) )   /*返回数组元素的个数 */

#define bitcheck8(var,bit) (var & ((u8)1 << (bit)))
#define bitset8(var,bitno) ((var) |= (u8)1 << (bitno))
#define bitclr8(var,bitno) ((var) &= ~((u8)1 << (bitno)))
#define bittoggle8(var,bit) ((var) ^= (u8)1 << (bit))

#define bitcheck16(var,bit) (var & ((u16)1 << (bit)))
#define bitset16(var,bitno) ((var) |= (u16)1 << (bitno))
#define bitclr16(var,bitno) ((var) &= ~((u16)1 << (bitno)))
#define bittoggle16(var,bit) ((var) ^= (u16)1 << (bit))

#define bitcheck32(var,bit) (var & ((u32)1 << (bit)))
#define bitset32(var,bitno) ((var) |= (u32)1 << (bitno))
#define bitclr32(var,bitno) ((var) &= ~((u32)1 << (bitno)))
#define bittoggle32(var,bit) ((var) ^= (u32)1 << (bit))

#define bitcheck64(var,bit) (var & ((u64)1 << (bit)))
#define bitset64(var,bitno) ((var) |= (u64)1 << (bitno))
#define bitclr64(var,bitno) ((var) &= ~((u64)1 << (bitno)))
#define bittoggle64(var,bit) ((var) ^= (u64)1 << (bit))
/* 得到指定地址上的一个变量 */ 
#define  ForceToU1t(x) (*((u8*)(&x)))
#define  ForceToU2t(x) (*((u16*)(&x)))
#define  ForceToU4t(x) (*((u32*)(&x)))
#define  ForceToU8t(x) (*((u64*)(&x)))
/* 求最大值和最小值 */
//#ifndef MAX
//#define  MAX( x, y ) ( ((x) > (y)) ? (x) : (y) ) 
//#endif
//#ifndef MIN
//#define  MIN( x, y ) ( ((x) < (y)) ? (x) : (y) )
//#endif

/* 得到一个field在结构体(struct)中的偏移量 */
#define FPOS( type, field )   ( (u32) &(( type *) 0)-> field ) 

/* 得到一个结构体中field所占用的字节数 */
#define FSIZ( type, field ) sizeof( ((type *) 0)->field )

/*按照LSB格式把两个字节转化为一个Word */
#define  FLIPW( ray ) ( (((word) (ray)[0]) * 256) + (ray)[1] )

/* 判断字符是不是10进值的数字 */
#define  DECCHK( c ) ((c) >= '0' && (c) <= '9')

/* 用于函数数组 */
typedef void (*function_table)(void);

#endif
