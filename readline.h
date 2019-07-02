/*******************************************************************************
* 文件名称: readline.h
* 功能描述: readline模块相关函数
* 其它说明: 独立模块，不依赖动态库和其他文件，OS要求Linux，Platform无要求                            				                    
* 编写日期: 2018-09-25
* 邮箱： firdin@yeah.net
* 修改历史: 
* 	修改版本     修改日期        修改人         修改内容
* ------------------------------------------------------------------------------
* 	 V0.1        2018-09-25       Firdin       创建初版，从github移植					
*******************************************************************************/
#ifndef _READLINE_H
#define _READLINE_H
 
/* 命令行历史文件 */
#define READLINE_HISTORY_FILE				"./readline_history_%d-%02d-%02d.log"		/* 由于同一系统可能有多个shell运作，故文件名依据PID添加 */
 
/* 历史记录上下翻方向控制 */
#define LINENOISE_HISTORY_NEXT 				0							//上翻
#define LINENOISE_HISTORY_PREV 				1							//下翻
 
#define DEFAULT_HISTORY_MAX_LEN 			200							/* 默认历史记录的长度 */
#define READLINEL_LINE_MAX_LEN 				4096						/* 一次读取行最大字节 */
 
/* 是否是C与其他语言混合编程 */
#ifdef __cplusplus
extern "C" {
#endif
 
/* 自动补全依赖结构体 */
typedef struct tab_auto_complete
{
  size_t len;
  char **cvec;
}tab_auto_complete;
 
/* 行编辑buffer数据结构体 */
typedef struct edit_line_buf
{
    char *b;
    int len;
}edit_line_buf_t;
 
/* readline依赖的数据结构 */
typedef struct read_line_db
{
    int ifd;            /* 终端标准输入描述符:0 */
    int ofd;            /* 终端标准输出描述符:1 */
    char *buf;          /* 当前终端行内容buffer指针 */
    size_t buflen;      /* 行最大长度 */
    const char *prompt; /* 终端命令行前缀buffer指针 */
    size_t plen;        /* 前缀buffer大小 */
    size_t pos;         /* 当前光标位置 */
    size_t oldpos;      /* 光标前一个位置 */
    size_t len;         /* 当前行内容大小 */
    size_t cols;        /* 终端列数 */
    size_t maxrows;     /* 最大行数（一行过长加回车显示） */
    int history_index;  /* 当前历史记录索引 */
}read_line_db_t;
 
/* 键值定义 */
enum KEY_ACTION
{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl + A */
	CTRL_B = 2,         /* Ctrl + B */
	CTRL_C = 3,         /* Ctrl + C */
	CTRL_D = 4,         /* Ctrl + D */
	CTRL_E = 5,         /* Ctrl + E */
	CTRL_F = 6,         /* Ctrl + F */
	CTRL_G = 7,			/* Ctrl + G */
	CTRL_H = 8,         /* Ctrl + H */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl + K */
	CTRL_L = 12,        /* Ctrl + L */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl + N */
	CTRL_P = 16,        /* Ctrl + p */
	CTRL_S = 19,		/* Ctrl + S */
	CTRL_T = 20,        /* Ctrl + T */
	CTRL_U = 21,        /* Ctrl + U */
	CTRL_W = 23,        /* Ctrl + W */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};	
 
/* 自动补全回调函数类型定义 */
typedef void(tab_completion_call_back)(const char *, tab_auto_complete *);
/* Ctrl+C按键回调函数 */
typedef void(ctrl_c_call_back_func)(const char*);
 
/* 函数声明：对外函数接口声明 */
int readline_insert_str(char* insert_str,...);
int readline_enable_raw_mode(int fd);
void readline_disable_raw_mode(int fd);
void readline_exit(void);
void readline_clear_all_screen(void);
void bcm_shell_register_complete_func(tab_completion_call_back *fn);
void readline_tab_complete_add_str(tab_auto_complete *tac, const char *str);
int readline_insert_char(read_line_db_t *l, char c);
void readline_cursor_move_left(read_line_db_t *l);
void readline_cursor_move_right(read_line_db_t *l);
void readline_cursor_move_home(read_line_db_t *l);
void readlinel_cursor_move_end(read_line_db_t *l);
void readline_history_search(read_line_db_t *l, int dir);
void readline_delete_from_right(read_line_db_t *l);
void readline_delete_from_left(read_line_db_t *l);
void readline_delete_all_from_left(read_line_db_t *l);
char *readline_entry(const char *prompt);
int readline_history_add(const char *line);
int readline_history_set_max_len(int len);
int readline_history_save(const char *filename);
int readline_history_record_file_load(const char *filename);
int readline_init(tab_completion_call_back *tab_call_back_func,ctrl_c_call_back_func* ctrl_c_call_back);
#ifdef __cplusplus
}
#endif
 
#endif /* end of _READLINE_H */
