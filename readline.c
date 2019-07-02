/*******************************************************************************
* 文件名称: readline.c
* 功能描述: readline模块相关函数
* 其它说明: 独立模块，不依赖动态库和其他文件，OS要求Linux，Platform无要求                       
* 实现途径：read/write标准I/O流实现输入/出字符读写；write屏幕code实现清屏、移动光标等操作。 				                    
* 编写日期: 2018-09-25
* 邮箱：firdin@yeah.net
* 修改历史: 
* 	修改版本	修改日期	修改人         修改内容
* ------------------------------------------------------------------------------
*	V0.1		2018-09-25	firdin       从github移植			
*******************************************************************************/
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
 
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
 
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdarg.h>
#include "readline.h"
 
static read_line_db_t *rlp;/* 刷新行buffer指针 */
static read_line_db_t l;
static unsigned int edit_line_flag = 0;
static ctrl_c_call_back_func* ctrl_c_func = NULL;/* 注册Ctrl+C组合键回调函数 */
static char *unsupported_term_type[] = {"dumb","cons25","emacs",NULL};/* 不支持的终端类型 */
static tab_completion_call_back *tab_completion_func = NULL;/* 自动补全回调函数指针 */
static char history_file_name[64];/* 记录命令行和结果的文件名 */
static struct termios orig_termios;/* 原有终端模式参数 */
unsigned int rawmode = 0;/* 命令行的RAW模式是否开启标示 */
static int history_max_len = DEFAULT_HISTORY_MAX_LEN;/* 默认历史记录长度 */
static int history_len = 0;/* 当前历史记录长度 */
static char **history = NULL;/* 历史记录：指针数组的指针（申请存放历史记录的字符串指针的数组空间，记录该空间的指针） */
 
 
/* 文件对内函数接口声明 */
static int readline_config_terminal(void);
static int readline_get_cursor_position(int ifd, int ofd);
static int readline_get_terminal_col(int ifd, int ofd);
static void readline_screen_coursor_beep(void);
static void readline_tab_buffer_free(tab_auto_complete *tac);
static int readline_tab_auto_complete(read_line_db_t *ls);
static void readline_edit_line_buffer_init(edit_line_buf_t *elb);
static void readline_edit_line_buffer_append(edit_line_buf_t *elb,const char *s,int len);
static void readline_edit_line_buffer_free(edit_line_buf_t *elb);
static void readline_refresh_line(read_line_db_t *l);
static int readline_edit_line(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
static char *readline_edit_line_no_tty(void);
static void readline_history_free(void);
 
/* ====================== 底层终端模式设置实现 ====================== */
 
/***********************************************************
* 函数名称: readline_config_terminal
* 函数功能: 判断终端类型是否支持
* 参数名称           类型          输入/输出          描述
*  
* 返回值:  如果是列表中不受支持的类型，则返回1，否则0
* 函数类型: 内部函数
*************************************************************/
static int readline_config_terminal(void)
{
    char *term = getenv("TERM");
    int j;
 
    if (term == NULL) return 0;
    for (j = 0; unsupported_term_type[j]; j++)
	{
        if (!strcasecmp(term,unsupported_term_type[j]))
		{
			return 1;
		}
	}
    return 0;
}
 
/***********************************************************
* 函数名称: readline_enable_raw_mode
* 函数功能: 打开终端raw模式
* 参数名称      类型    输入/输出       描述
*   fd			int       IN		Linux文件描述符
* 返回值:  
* 函数类型: 外部可调用    Raw mode: 1960 magic shit.
*************************************************************/
int readline_enable_raw_mode(int fd)
{
    struct termios raw;
 
	/* 判断是否是终端 */
    if (!isatty(STDIN_FILENO))
		return -1;
	
	/* 获取当前终端的tty参数 */
    if (-1 == tcgetattr(fd,&orig_termios)) 
		return -1;
 
	/* 在终端原有模式基础上进行修改添加 */
    raw = orig_termios;
    /* 输入模式:no break, no CR to NL, no parity check, no strip char,no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* 输出模式:disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* 流控制:8bit */
    raw.c_cflag |= (CS8);
    /* 本地模式：打开回显、规范显示、定义的输入处理。 */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN );
    /* 控制字符设置 */
    raw.c_cc[VMIN] = 1;	 /* 非规范读取数据 */
	raw.c_cc[VTIME] = 0; /* 不处理计时器输入 */
	raw.c_cc[VINTR] = 1; /* 打开进程自身信号捕获，用于捕获 Ctrl+C */
	
    /* TCSAFLUSH:等待所有数据传输结束,清空输入输出缓冲区再改变终端属性 */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) 
		return -1;
    rawmode = 1;
 
    return 0;
}
 
/***********************************************************
* 函数名称: readline_disable_raw_mode
* 函数功能: 关闭终端raw模式
* 参数名称       类型     输入/输出          描述
*    fd          int         IN          Linux文件描述符
* 返回值:  
* 函数类型:外部可调用
*************************************************************/
void readline_disable_raw_mode(int fd)
{
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
}
 
/***********************************************************
* 函数名称: readline_exit
* 函数功能: 注册退出时函数
* 参数名称       类型     输入/输出          描述
*  
* 返回值:  
* 函数类型: 外部调用
*************************************************************/
void readline_exit(void)
{
    readline_disable_raw_mode(STDIN_FILENO);
    readline_history_free();
}
 
/***********************************************************
* 函数名称: readline_get_cursor_position
* 函数功能: 获取光标位置
* 参数名称       类型     输入/输出          描述
*    ifd          int         IN          Linux文件描述符(标准输入)
*    ofd          int         IN          Linux文件描述符(标准输出)
* 返回值:  
* 函数类型: 内部函数
* 说明：该函数的调用发生在无法获取当前终端的尺寸数据
*************************************************************/
static int readline_get_cursor_position(int ifd, int ofd)
{
    char buf[32];
    int cols, rows;
    unsigned int i = 0;
 
    if (write(ofd, "\x1b[6n", 4) != 4)
		return -1;
 
    while (i < sizeof(buf)-1)
	{
        if (read(ifd,buf+i,1) != 1)
			break;
        if (buf[i] == 'R')
			break;
        i++;
    }
    buf[i] = '\0';
 
    if (buf[0] != ESC || buf[1] != '[')
		return -1;
    if (sscanf(buf+2,"%d;%d",&rows,&cols) != 2)
		return -1;
    return cols;
}
 
/***********************************************************
* 函数名称: readline_get_terminal_col
* 函数功能: 获取当前终端的尺寸参数
* 参数名称       类型     输入/输出          描述
*    ifd          int         IN          Linux文件描述符(标准输入)
*    ofd          int         IN          Linux文件描述符(标准输出)
* 返回值: 返回终端的列数 
* 函数类型: 内部函数
* 说明：该函数的调用发生在无法获取当前终端的尺寸数据
*************************************************************/
static int readline_get_terminal_col(int ifd, int ofd)
{
    struct winsize ws;				/* 终端I/O结构体 */
 
    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)/* 如果ioctl调用失败，则通过以下方式获取列数 */
	{
        int start,cols;
        start = readline_get_cursor_position(ifd,ofd);
        if (start == -1)
			goto failed;
		
        if (write(ofd,"\x1b[999C",6) != 6)
			goto failed;
        cols = readline_get_cursor_position(ifd,ofd);
        if (cols == -1)
			goto failed;
 
        if (cols > start)
		{
            char seq[32];
            snprintf(seq,32,"\x1b[%dD",cols-start);
            if (write(ofd,seq,strlen(seq)) == -1)
			{
                /* 写失败无法恢复 */
            }
        }
        return cols;
    }
	else
	{
        return ws.ws_col;
    }
 
failed:
    return 80;
}
 
/***********************************************************
* 函数名称: readline_clear_all_screen
* 函数功能: 清除屏幕内容，类似linux的clear命令
* 参数名称       类型     输入/输出          描述
* 
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_clear_all_screen(void)
{
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0)
	{
        /* 写失败无法恢复 */
    }
}
 
/***********************************************************
* 函数名称: readline_screen_coursor_beep
* 函数功能: 光标闪烁，用于等待操作完成
* 参数名称       类型     输入/输出          描述
* 
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_screen_coursor_beep(void)
{
    fprintf(stderr, "\x7");
    fflush(stderr);
}
 
/* ============================== Tab键自动补全功能 ================================ */
 
/***********************************************************
* 函数名称: readline_tab_buffer_free
* 函数功能: 释放tab申请的buffer
* 参数名称       类型            输入/输出          描述
*   tac     tab_auto_complete*      IN      自动补全数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_tab_buffer_free(tab_auto_complete *tac)
{
    size_t i;
    for (i = 0; i < tac->len; i++)
        free(tac->cvec[i]);
    if (tac->cvec != NULL)
        free(tac->cvec);
}
 
/***********************************************************
* 函数名称: readline_tab_auto_complete
* 函数功能: 自动补全处理
* 参数名称       类型            输入/输出          描述
*   tac     tab_auto_complete*      IN      自动补全数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static int readline_tab_auto_complete(read_line_db_t *ls)
{
    tab_auto_complete tac = { 0, NULL };
    int nread;
    char c = 0;
 
    tab_completion_func(ls->buf,&tac);/* 回调注册函数，查询当前要内容，判断依据由回调函数实现 */
    if (tac.len == 0)
	{
        readline_screen_coursor_beep();	/* 如无对应内容，光闪烁并返回 */
    }
	else
	{
        size_t stop = 0, i = 0;
		char* new_buf = NULL;
		
        while(!stop)
		{
            /* Show completion or original buffer */
            if (i != tac.len)
			{
                read_line_db_t saved = *ls;
 
				ls->len = ls->pos = ls->len + strlen(tac.cvec[i]);
				new_buf = (char*)malloc(sizeof(char) * (ls->len + 1));						//行末加'\0'
                memcpy(new_buf,ls->buf,strlen(ls->buf));									//拷贝内容，不拷贝'\0'
                memcpy(new_buf + strlen(ls->buf) - 1,tac.cvec[i],strlen(tac.cvec[i])+1);	//将原有buffer最后一个字符覆盖掉,同时将自动补全关键字字符串末尾的'\0'拷贝过去
                ls->buf = new_buf;
				ls->len --;																	/* 自动补全时，依赖的字符需要被覆盖掉，光标前移一位 */
				ls->pos --;
                readline_refresh_line(ls);        											//刷新自动补全内容到屏幕
				free(new_buf);
				
				/* 还原行编辑 */
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            }
			else
			{
                readline_refresh_line(ls);
            }
 
            nread = read(ls->ifd,&c,1);
            if (nread <= 0)
			{
                readline_tab_buffer_free(&tac);
                return -1;
            }
 
            switch(c)
			{
                case TAB: /* tab */
                    i = (i+1) % (tac.len+1);
                    if (i == tac.len)
						readline_screen_coursor_beep();
                    break;
                case ESC: /* escape */
                    /* Re-show original buffer */
                    if (i < tac.len) readline_refresh_line(ls);
                    stop = 1;
                    break;
                default:
					//动态补全，需要自动补全回调函数注意补全的位置
					if (i != tac.len)
					{
                        snprintf(ls->buf + ls->len - 1,\
								 ls->buflen - ls->len + 1,\
								 "%s",\
								 tac.cvec[i]);
                        ls->len = ls->pos = strlen(ls->buf);
                    }
					
                    stop = 1;
                    break;
            }
        }
    }
 
    readline_tab_buffer_free(&tac);
    return c;
}
 
/***********************************************************
* 函数名称: readline_tab_complete_add_str
* 函数功能: 自动补全字符串添加
* 参数名称       类型                   输入/输出          描述
*   fn     tab_completion_call_back*      IN         自动补全函数指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_tab_complete_add_str(tab_auto_complete *tac, const char *str)
{
    size_t len = strlen(str);
    char *copy, **cvec;
 
    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(tac->cvec,sizeof(char*)*(tac->len+1));
    if (cvec == NULL)
	{
        free(copy);
        return;
    }
	
    tac->cvec = cvec;
    tac->cvec[tac->len++] = copy;
}
 
/* ================================ 行编辑相关函数 =========================== */
 
/***********************************************************
* 函数名称: readline_edit_line_buffer_init
* 函数功能: 行编辑buffer初始化函数
* 参数名称       类型               输入/输出          描述
*   elb     edit_line_buf_t *      IN         行编辑结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_edit_line_buffer_init(edit_line_buf_t *elb)
{
    elb->b = NULL;
    elb->len = 0;
}
 
/***********************************************************
* 函数名称: readline_edit_line_buffer_append
* 函数功能: 行编辑buffer内容添加函数
* 参数名称       类型               输入/输出          描述
*   elb     edit_line_buf_t *      IN         行编辑结构体指针
*    s            char *                IN         buffer指针
*   len            int                  IN         buffer长度
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_edit_line_buffer_append(edit_line_buf_t *elb,const char *s,int len)
{
    char *new = realloc(elb->b,elb->len+len);
 
    if (new == NULL) return;
    memcpy(new+elb->len,s,len);
    elb->b = new;
    elb->len += len;
}
 
/***********************************************************
* 函数名称: readline_edit_line_buffer_free
* 函数功能: 行编辑buffer空间释放
* 参数名称       类型               输入/输出          描述
*   elb     edit_line_buf_t *      IN         行编辑结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_edit_line_buffer_free(edit_line_buf_t *elb)
{
    free(elb->b);
}
 
/***********************************************************
* 函数名称: readline_refresh_line
* 函数功能: 刷新行
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static void readline_refresh_line(read_line_db_t *l)
{
	if(NULL == l)
	{
		return ;
	}
	
	rlp = l;											/* 存储刷新 */
    char seq[64];
    int plen = strlen(l->prompt);									/* 命令行前缀长度 */
    int rows = (plen+l->len+l->cols-1)/l->cols;						/* 最后一行的长度 */
    int rpos = (plen+l->oldpos+l->cols)/l->cols;					/* 当前位置：行数 */
    int rpos2;
    int col;
    int old_rows = l->maxrows;										/* 上一行 */
    int fd = l->ofd, j;
    edit_line_buf_t elb;
 
    if (rows > (int)l->maxrows)
		l->maxrows = rows;
 
    /* 1、清除所有行的内容（从最后一行开始清除） */
    readline_edit_line_buffer_init(&elb);
    if (old_rows-rpos > 0)
	{
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);						/* \x1b[%dB:光标下移 */
        readline_edit_line_buffer_append(&elb,seq,strlen(seq));
    }
 
    for (j = 0; j < old_rows-1; j++)/* 往下移动所输入的行数 */
	{
        snprintf(seq,64,"\r\x1b[0K\x1b[1A");							/* \x1b[0K:清除从光标到结尾的字符, \x1b[1A:上移一位 */					
        readline_edit_line_buffer_append(&elb,seq,strlen(seq));
    }
 
	/* 清除第一行 */
    snprintf(seq,64,"\r\x1b[0K");										/* \x1b[0K:清除光标至行末尾的字符 */
    readline_edit_line_buffer_append(&elb,seq,strlen(seq));
    readline_edit_line_buffer_append(&elb,l->prompt,strlen(l->prompt));
    readline_edit_line_buffer_append(&elb,l->buf,l->len);
 
    /* 如果行数刚好填满当前终端，则换行避免前缀显示不出来 */
    if (l->pos &&
        l->pos == l->len &&
        (l->pos+plen) % l->cols == 0)
    {
        readline_edit_line_buffer_append(&elb,"\n",1);
        snprintf(seq,64,"\r");
        readline_edit_line_buffer_append(&elb,seq,strlen(seq));
        
		/* 行数加1 */
		rows++;
        if (rows > (int)l->maxrows)
			l->maxrows = rows;
    }
 
    /* 当前光标相对位置 */
    rpos2 = (plen+l->pos+l->cols)/l->cols;
 
    /* 光标上移至(本次编辑行)起始位置 */
    if (rows-rpos2 > 0)
	{
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);				/* \x1b[%dA:光标上移 */
        readline_edit_line_buffer_append(&elb,seq,strlen(seq));
    }
 
    col = (plen+(int)l->pos) % (int)l->cols;
    if (col)
        snprintf(seq,64,"\r\x1b[%dC", col);						/* 如果列数不为0，则右移，否则光标左移至行首 */
    else
        snprintf(seq,64,"\r");
    readline_edit_line_buffer_append(&elb,seq,strlen(seq));
 
    l->oldpos = l->pos;
 
    if (write(fd,elb.b,elb.len) == -1)
	{
		/* 写错误后无法恢复 */
	} 
    readline_edit_line_buffer_free(&elb);
}
 
/***********************************************************
* 函数名称: readline_insert_char
* 函数功能: 在当前行插入字符
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *           IN         readline数据结构体指针
*    c           char                  IN         要插入的字符
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
int readline_insert_char(read_line_db_t *l, char c)
{
    if (l->len < l->buflen)
	{
        if (l->len == l->pos)
		{
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            readline_refresh_line(l);
        }
		else
		{
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            readline_refresh_line(l);
        }
    }
    return 0;
}
 
/***********************************************************
* 函数名称: readline_cursor_move_left
* 函数功能: 将光标左移
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_cursor_move_left(read_line_db_t *l)
{
    if(l->pos > 0)
	{
        l->pos--;
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_cursor_move_right
* 函数功能: 将光标右移
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_cursor_move_right(read_line_db_t *l)
{
    if(l->pos != l->len)
	{
        l->pos++;
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_cursor_move_home
* 函数功能: 将光标移动到行首
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_cursor_move_home(read_line_db_t *l)
{
    if(l->pos != 0)
	{
        l->pos = 0;
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readlinel_cursor_move_end
* 函数功能: 将光标移动到行末
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readlinel_cursor_move_end(read_line_db_t *l)
{
    if(l->pos != l->len)
	{
        l->pos = l->len;
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_history_search
* 函数功能: 历史记录查找
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
*   dir          int                   IN         查找方向：LINENOISE_HISTORY_NEXT:往后查找
*															LINENOISE_HISTORY_PREV:往前查找
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_history_search(read_line_db_t *l, int dir)
{
    if(history_len > 1)
	{
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
 
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if(l->history_index < 0)
		{
            l->history_index = 0;
            return;
        }
		else if(l->history_index >= history_len)
		{
            l->history_index = history_len-1;
            return;
        }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_delete_from_right
* 函数功能: 从右侧删除字符（类似windows的delete）
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_delete_from_right(read_line_db_t *l)
{
    if (l->len > 0 && l->pos < l->len)
	{
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_delete_from_left
* 函数功能: 从左侧删除字符（类似windows的backspace）即退格删除
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_delete_from_left(read_line_db_t *l)
{
    if (l->pos > 0 && l->len > 0)
	{
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        readline_refresh_line(l);
    }
}
 
/***********************************************************
* 函数名称: readline_delete_all_from_left
* 函数功能: 删除左侧所有的字符
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
void readline_delete_all_from_left(read_line_db_t *l)
{
    size_t old_pos = l->pos;
    size_t diff;
 
    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    readline_refresh_line(l);
}
 
/***********************************************************
* 函数名称: readline_edit_line
* 函数功能: 行编辑处理函数
* 参数名称     类型         输入/输出    描述
* stdin_fd     int            IN        标准输入 
* stdout_fd    int            IN        标准输出
* buf		   char*          IN        buffer指针，存放行内容buffer的指针
* buflen       size_t         IN        buffer长度（4096）
* prompt       const char*    IN        前缀buffer指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static int readline_edit_line(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
	edit_line_flag = 1;
    
    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.cols = readline_get_terminal_col(stdin_fd, stdout_fd);		/* 获取列数 */
    l.maxrows = 0;
    l.history_index = 0;											
 
    l.buf[0] = '\0';
    l.buflen--;														/* 保证最后一位可以写入结束符'\0' */
 
    readline_history_add("");										/* 为避免段错误，先写入空buffer */
 
    if (write(l.ofd,prompt,l.plen) == -1)							/* 打印前缀 */
	{
		edit_line_flag = 0;
		return -1;
	}
	
    while(1)
	{
        char c;
        int nread;
        char seq[3];				/* 字符读取，当为控制字符时，可能有三到四个字符 */
        nread = read(l.ifd,&c,1);
        if (nread <= 0)
		{
			edit_line_flag = 0;
			return l.len;
		}
        /* 仅当回调函数注册时才能实现自动补全 */
        if (c == 9 && tab_completion_func != NULL)
		{
            c = readline_tab_auto_complete(&l);		/* 调用自动补全 */
 
            if (c == 0)					/* 无自动补全内容 */
				continue;
        }
 
        switch(c)
		{
			case ENTER:  								/* 回车键 */
				history_len--;
				free(history[history_len]);
				readlinel_cursor_move_end(&l);
				edit_line_flag = 0;
				return (int)l.len;
			case CTRL_C:								/* 强制停止键 */
				if(NULL != ctrl_c_func)
					ctrl_c_func("Recv SIGINT,Stop UDP Recv!");
				else
					readline_insert_str("Recv SIGINT,Call Back Func is NULL\n");
				edit_line_flag = 0;
				return 0;
			/* linux终端模式过于混乱，delete和backspace无法区分，
			   windows的SecurtCRT下backspace和delete均为退格键 */
			case BACKSPACE:								/* 退格键 */
			case CTRL_H:								/* Ctrl+H键，VT100终端退格键值 */
				readline_delete_from_left(&l);
				break;
			case CTRL_D:     							/* Ctrl+D:实现windows下的delete功能，与终端类型无关 */
				if (l.len > 0)
				{
					readline_delete_from_right(&l);
				}
				else
				{
					history_len--;
					free(history[history_len]);
					edit_line_flag = 0;
					return -1;
				}
				break;
			case CTRL_T:    							/* Ctrl+T:光标与前一个位置的字符 */
				if (l.pos > 0 && l.pos < l.len)
				{
					int aux = buf[l.pos-1];
					buf[l.pos-1] = buf[l.pos];
					buf[l.pos] = aux;
					if (l.pos != l.len-1) l.pos++;
					readline_refresh_line(&l);
				}
				break;
			case ESC:    								/* 功能键包含多个字符 */
				if (read(l.ifd,seq,1) == -1)
					break;
				if (read(l.ifd,seq+1,1) == -1)
					break;
				if (seq[0] == '[')
				{
					if (seq[1] >= '0' && seq[1] <= '9')
					{
						if (read(l.ifd,seq+2,1) == -1)				/* 读取后续的字符，合并后判断 */
							break;
						if (seq[2] == '~')
						{
							switch(seq[1])
							{
								case '3': 							/* Linux命令行模式下的删除键 */
									readline_delete_from_right(&l);
									break;
							}
						}
					}
					else
					{
						switch(seq[1])
						{
							case 'A': /* 上翻键：历史记录上翻 */
								readline_history_search(&l, LINENOISE_HISTORY_PREV);
								break;
							case 'B': /* 下翻键：历史记录下翻 */
								readline_history_search(&l, LINENOISE_HISTORY_NEXT);
								break;
							case 'C': /* 右移：将光标右移 */
								readline_cursor_move_right(&l);
								break;
							case 'D': /* 左移：将光标左移 */
								readline_cursor_move_left(&l);
								break;
							case 'H': /* 将光标移动至行首 */
								readline_cursor_move_home(&l);
								break;
							case 'F': /* 将光标移动至行末 */
								readlinel_cursor_move_end(&l);
								break;
						}
					}
				}
				else if (seq[0] == 'O')
				{
					switch(seq[1])
					{
						case 'H': /* Linux命令行模式下的home键 */
							readline_cursor_move_home(&l);
							break;
						case 'F': /* Linux命令行模式下的end键*/
							readlinel_cursor_move_end(&l);
							break;
					}
				}
				break;
			default:
				if (readline_insert_char(&l,c))				/* 输入非控制字符时:在光标位置插入该字符 */
				{
					edit_line_flag = 0;
					return -1;
				}
				break;
			case CTRL_U: 										/* Ctrl+U:删除整行的字符 */
				buf[0] = '\0';
				l.pos = l.len = 0;
				readline_refresh_line(&l);
				break;
			case CTRL_K: 										/* Ctrl+K:删除光标右侧所有字符 */
				buf[l.pos] = '\0';
				l.len = l.pos;
				readline_refresh_line(&l);
				break;
			case CTRL_S: 										/* Ctrl+S:将光标移动到行首 */
				readline_cursor_move_home(&l);
				break;
			case CTRL_E: 										/* Ctrl+E:将光标移动行末 */
				readlinel_cursor_move_end(&l);
				break;
			case CTRL_L:										/* Ctrl+L:清除屏幕显示 */
				readline_clear_all_screen();
				readline_refresh_line(&l);
				break;
			case CTRL_W: 										/* Ctrl+W:删除光标左侧所有字符 */
				readline_delete_all_from_left(&l);
				break;
        }
    }
	edit_line_flag = 0;
    return l.len;
}
 
/***********************************************************
* 函数名称: readline_edit_line_no_tty
* 函数功能: 无终端情况下readline处理
* 参数名称       类型               输入/输出          描述
*   elb     read_line_db_t *      IN         readline数据结构体指针
* 返回值: 
* 函数类型: 外部可调用
*************************************************************/
static char *readline_edit_line_no_tty(void)
{
    char *line = NULL;
    size_t len = 0, maxlen = 0;
 
    while(1)
	{
        if (len == maxlen)
		{
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL)
			{
                if (oldval) 
					free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n')
		{
            if (c == EOF && len == 0)
			{
                free(line);
                return NULL;
            } 
			else
			{
                line[len] = '\0';
                return line;
            }
        }
		else
		{
            line[len] = c;
            len++;
        }
    }
}
 
/***********************************************************
* 函数名称: readline_insert_str
* 函数功能: 插入字符到当前realine（实现在任意情况下插入，线程与进程不互斥）
* 参数名称          类型       输入/输出          描述
* insert_str        char *        IN			插入字符指针
*
* 返回值: char* 终端输入的字符串（已通过readline处理）指针
* 函数类型: 外部可调用
* 依赖全局变量:	rlp
* 函数说明：当进程与线程都需要回显时，进程始终处于readline模式，
*           此时线程printf时会出问题，使用此函数可解决回显乱码问题。
*			该函数显示方式：删除当前编辑行所有屏幕内容，将线程要显示的内容刷新到屏幕上，
*			然后再将刷新前的内容重新写到屏幕结尾（动态刷新），实现插入式显示。
*************************************************************/
int readline_insert_str(char* insert_str,...)
{
	if(NULL == insert_str || NULL == rlp)
	{
		return -1;
	}
 
	va_list vl;
	char seq[5120];
	char vlb[4096];
	edit_line_buf_t lbuf;
	int fd = 0;
	
	if(edit_line_flag)
		fd = rlp->ofd;
	else
		fd = STDOUT_FILENO;
	
	/* 初始化buffer空间 */
	readline_edit_line_buffer_init(&lbuf);
	
	/* 可变参数拼接 */
	memset(vlb,0,sizeof(vlb));
	va_start(vl,insert_str);
	vsprintf(vlb,insert_str, vl);
    va_end(vl);
		
	/* 1.回退光标 */
	memset(seq,0,sizeof(seq));
	
	//清除光标所在行所有字符：\x1b[2K
	if(edit_line_flag)
	{
		snprintf(seq,5120,"\x1b[2K\x1b[%dD%s\n",(int)(rlp->len + rlp->plen),vlb);
		readline_edit_line_buffer_append(&lbuf,seq,strlen(seq));
		readline_edit_line_buffer_append(&lbuf,rlp->prompt,rlp->plen);
	}
	else
	{
		snprintf(seq,5120,"%s\n",vlb);
		readline_edit_line_buffer_append(&lbuf,seq,strlen(seq));
	}
	
	/* 将回显内容显示到终端上 */
	if (write(fd,lbuf.b,lbuf.len) == -1)
	{
		/* 写错误后无法恢复 */
		system("echo \"write stdout error!\" > readline-error.txt");
	}
	
	/* 释放buffer空间 */
	readline_edit_line_buffer_free(&lbuf);
	
	/* 3.恢复插入前refresh_line或利用新建立的buffer获取readline */
	if(edit_line_flag)
		readline_refresh_line(rlp);
	
	return 0;
}
 
/***********************************************************
* 函数名称: readline_entry
* 函数功能: readline入口函数
* 参数名称       类型    输入/输出          描述
*   prompt     char *      IN         前缀buffer指针
* 返回值: char* 终端输入的字符串（已通过readline处理）指针
* 函数类型: 外部可调用
*************************************************************/
char *readline_entry(const char *prompt)
{
    char buf[READLINEL_LINE_MAX_LEN];
    int count;
 
    if (!isatty(STDIN_FILENO))/* 当前终端分配的标准输出非终端 */
	{
        return readline_edit_line_no_tty();
    }
	else if (readline_config_terminal())/* 判断终端类型为非支持类型（不可设置RAW模式的终端） */
	{
        size_t len;
 
        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,READLINEL_LINE_MAX_LEN,stdin) == NULL)
			return NULL;
        len = strlen(buf);
		
		/* 过滤回车和光标置位字符 */
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		{
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);		
    }
	else/* 终端类型受支持则进入readline处理 */
	{		
		if (readline_enable_raw_mode(STDIN_FILENO) == -1)
			return NULL;
		count = readline_edit_line(STDIN_FILENO,\
									STDOUT_FILENO,\
									buf,\
									READLINEL_LINE_MAX_LEN,\
									prompt);
		readline_disable_raw_mode(STDIN_FILENO);
		printf("\n");
		
        if (count == -1)
			return NULL;
        return strdup(buf);			/* 将字符串动态拷贝并返回malloc指针，注意用完后要释放 */
    }
}
 
/* =========================================== 命令行历史记录相关函数 ========================================= */
 
/***********************************************************
* 函数名称: readline_history_free
* 函数功能: 释放历史记录所申请的buffer
* 参数名称       类型    输入/输出          描述
* 
* 返回值: 
* 函数类型: 内部函数
*************************************************************/
static void readline_history_free(void)
{
    if (history)
	{
        int j;
 
        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}
 
 
/***********************************************************
* 函数名称: readline_history_add
* 函数功能: 添加历史记录到缓存中
* 参数名称       类型         输入/输出          描述
*   line      const char *       IN         要记录的命令行buffer指针
* 返回值: -1：申请buffer失败或历史记录未初始化
		  0：无需添加
*		  1：添加历史记录成功
* 函数类型: 内部函数
*************************************************************/
int readline_history_add(const char *line)
{
    char *linecopy;
 
    if (history_max_len == 0)
		return -1;
 
    if (NULL == history)/* 申请指针数组的buffer */
	{
        history = malloc(sizeof(char*) * history_max_len);
        if (history == NULL)
			return -1;
        memset(history,0,(sizeof(char*) * history_max_len));
    }
 
    if (history_len && !strcmp(history[history_len-1],line))/* 命令相同则不添加，不同则添加 */
		return 0;
 
    linecopy = strdup(line);/* 动态拷贝 */
 
    if (!linecopy)
		return -1;
	
    if (history_len == history_max_len)	/* 如果超出历史记录的长度，则覆盖原有的内容 */
	{
        free(history[0]);				/* 释放原有的buffer空间，避免内存碎片 */
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));	/* 移动存储空间，覆盖history[0]，留出history[history_len] */
        history_len--;
    }
	
	/* 在历史记录末尾追加 */
    history[history_len] = linecopy;
    history_len++;
    return 1;
}
 
/***********************************************************
* 函数名称: readline_history_set_max_len
* 函数功能: 重新设置历史记录缓存buffer大小
* 参数名称   类型   输入/输出          描述
*   len      int       IN         要设置的历史记录长度
* 返回值: 0：参数错误
*		  1：成功
* 函数类型: 
*************************************************************/
int readline_history_set_max_len(int len)
{
    char **new;
 
    if (len < 1)
		return 0;
    if (history)
	{
        int tocopy = history_len;
 
        new = malloc(sizeof(char*)*len);
        if (new == NULL)
			return 0;
 
        /* 从头释放多余的空间（多出部分不作保存） */
        if (len < tocopy)
		{
            int j;
 
            for (j = 0; j < tocopy-len; j++)
				free(history[j]);
            tocopy = len;
        }
		
		/* 将旧数据拷贝到新buffer中，并释放旧的空间 */
        memset(new,0,sizeof(char*)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char*)*tocopy);
        free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}
 
/***********************************************************
* 函数名称: readline_history_save
* 函数功能: 重新设置历史记录缓存buffer大小
* 参数名称           类型        输入/输出          描述
* history_line    const char *     IN         要记录的内容
* 返回值: -1：参数错误
*		  0：成功
* 函数类型: 
*************************************************************/
int readline_history_save(const char *history_line)
{
	time_t timep;
	struct tm *p;
	char write_buffer[64];
	
	if(NULL == history_line)
	{
		readline_insert_str("History Recorder NULL!\n");
		return -1;
	}
	
	if(0 == strlen(history_file_name))
	{
		readline_insert_str("History File Not Inited!\n\r");
		return -1;
	}
	
    mode_t old_umask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
    FILE *fp;
 
    fp = fopen(history_file_name,"a+");	/* 追加内容到结尾 */
    umask(old_umask);
    if (fp == NULL)
		return -1;
    chmod(history_file_name,S_IRUSR|S_IWUSR);	/* 修改文件属性：读/写 */
 
	/* 获取时间并写入文件中 */	
	time(&timep);
    p=localtime(&timep);
    if(p == NULL)
    {
        readline_insert_str("Get Local Time Failed!\n\r");
        return -1;
    }
	
	memset(write_buffer,0,sizeof(write_buffer));
	sprintf(write_buffer,"[%02d:%02d:%02d]#",
		(p->tm_hour)%24,
		p->tm_min,
		p->tm_sec);
	/* 将时间和内容写入文件 */
    fprintf(fp,"%s %s\n",write_buffer,history_line);
	
    fclose(fp);
    return 0;
}
 
/***********************************************************
* 函数名称: readline_history_record_file_load
* 函数功能: 初始化记录历史命令的文件，从文件中恢复
* 参数名称         类型        输入/输出          描述
*   filename    const char *     IN         文件名str指针
* 返回值: -1：参数错误
*		  0：成功
* 函数类型: 
*************************************************************/
int readline_history_record_file_load(const char *filename)
{
    FILE *fp = fopen(filename,"r");
    char buf[READLINEL_LINE_MAX_LEN];
 
    if (fp == NULL)
		return -1;
 
	/* 遍历文件：最终将历史记录末尾500行（500行是历史记录最大值）加载到缓存中 */
    while (fgets(buf,READLINEL_LINE_MAX_LEN,fp) != NULL)
	{
        char *p;
 
        p = strchr(buf,'\r');
        if (!p)
			p = strchr(buf,'\n');
        if (p)
			*p = '\0';
        readline_history_add(buf);/* 从文件中恢复历史记录 */
    }
 
    fclose(fp);
    return 0;
}
 
/***********************************************************
* 函数名称: readline_init
* 函数功能: 初始化shell
* 参数名称                 类型                输入/输出          描述
* tab_call_back_func  tab_completion_call_back*       IN         tab补全回调函数
* 返回值: -1：参数错误
*		  0：成功
* 函数类型: 
*************************************************************/
int readline_init(tab_completion_call_back *tab_call_back_func,ctrl_c_call_back_func* ctrl_c_call_back)
{
	char write_buffer[256];
	time_t timep;
	struct tm *p;
	char *wday[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	
	/* 注册回调函数 */
	if(NULL != tab_call_back_func)
	{
		tab_completion_func = tab_call_back_func;
	}
	else
		printf("Tab Key Call Func is NULL!\n\r");
	
	time(&timep);
    p=localtime(&timep);
    if(p == NULL)
    {
        printf("Get Local Time Failed!\n\r");
        return -1;
    }
	
	if(NULL != ctrl_c_call_back)
		ctrl_c_func = ctrl_c_call_back;
	else
		printf("Ctrl+C Call Back is NULL!\n\r");
	
	/* 历史记录文件创建、初始化 */
	memset(history_file_name,0,sizeof(history_file_name));
	sprintf(history_file_name,READLINE_HISTORY_FILE,\
							(1900+p->tm_year),\
							(1+p->tm_mon),\
							p->tm_mday);
 
	if(access(history_file_name,0))
		printf("Create readline History File:%s\n\r",history_file_name);
	else
		printf("Open with history file:%s\n\r",history_file_name);
	
	
	FILE* fd=fopen(history_file_name,"a+");
	if(NULL == fd)
	{
		printf("History File Open Failed!\n\r");
		return -1;
	}
	
	sprintf(write_buffer,">>>>>Command History Recorder<<<<<\n\
****Local Time:%d-%02d-%02d %s %02d:%02d:%02d****\n****ReadLine PID:%d****\n",
        (1900+p->tm_year),
		(1+p->tm_mon),
		p->tm_mday,
		wday[p->tm_wday],
		(p->tm_hour)%24,
		p->tm_min,
		p->tm_sec,
		getpid());
	
	if( 0 == fwrite(write_buffer,strlen(write_buffer),1,fd))
	{
		printf("Write History File Failed!\n\r");
		if(NULL != fd)
			fclose(fd);
		return -1;
	}
	
	fclose(fd);
 
	return 0;
}
