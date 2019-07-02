/*******************************************************************************
* 文件名称: main.c
* 功能描述: readline模块简单应用
* 其它说明: 独立模块，不依赖动态库和其他文件，OS要求Linux，Platform无要求                            				                    
* 编写日期: 2018-09-25
* 邮箱： firdin@yeah.net
* 修改历史: 
* 	修改版本     修改日期        修改人         修改内容
* ------------------------------------------------------------------------------
* 	 V0.1        2018-09-25       Firdin       创建初版，从github移植					
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "readline.h"
 
#define PROMT_CONTENT           "[firdin@readline]#"
 
/***********************************************************
* 函数名称: tab_func
* 函数功能: 自动补全回调函数
* 参数名称      类型          输入/输出          描述
*    src        const char*          IN         自动补全时，行编辑buffer指针
*    des  tab_auto_complete *    IN                     补全buffer，临时buffer结构体
* 返回值: -1：参数错误
*                 0：成功
* 函数类型:
*************************************************************/
void tab_func(const char *src,tab_auto_complete *des)
{
    /* 如果一行最后一个字符是‘h’ */
    if(!strncmp((src+(strlen(src) - 1)),"h",1))
    {
            readline_tab_complete_add_str(des,"hello");
            readline_tab_complete_add_str(des,"hi");
    }
 
 
    if(!strncmp((src+(strlen(src)-1)),"w",1))
    {
            readline_tab_complete_add_str(des,"world");
    }
}
 
/***********************************************************
* 函数名称: ctrl_c
* 函数功能: linux下输入Ctrl+C组合键时执行的函数
* 参数名称      类型          输入/输出          描述
*    src        const char*          IN         回显内容，可依据需求对typedef进行修改
* 返回值:无
* 函数类型:
*************************************************************/
void ctrl_c(const char* src)
{
    readline_insert_str("Ctrl + C Call function! input:%s",src);
 
    /* your code */
}
 
 
/* 程序入口 */
int main()
{
    char* res = NULL;
	//初始化readline
    if(readline_init(tab_func,ctrl_c))
    {
            printf("[Readline]:Init Failed!\n");
            return -1;
    }
	//注册退出函数
    atexit(readline_exit);
	
	//循环readline
    while(1)
    {
            res = readline_entry(PROMT_CONTENT);
            if(!strncasecmp(res,"exit",strlen("exit")))
            {
                    readline_history_save(res);
                    if(NULL != res)
                    {
                            free(res);
                            res = NULL;
                    }
                    printf("Readline Exited!\n\r");
                    return 0;
            }
            if(strlen(res)>0)
            {
                    printf("echo>%s\n\r",res);
                    readline_history_save(res);
            }
            if(NULL != res)
            {
                    free(res);
                    res = NULL;
            }
    }
}
