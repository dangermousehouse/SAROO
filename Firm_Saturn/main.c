/*
 * Sega Saturn cartridge flash tool
 * by Anders Montonen, 2012
 *
 * Original software by ExCyber
 * Graphics routines by Charles MacDonald
 *
 * Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)
 */

#include "main.h"
#include "smpc.h"
#include "vdp2.h"

/**********************************************************/


u32 BE32(void *ptr)
{
	u8 *b = (u8*)ptr;
	return (b[0]<<24) | (b[1]<<16) | (b[2]<<8) | b[3];
}


u32 LE32(void *ptr)
{
	u8 *b = (u8*)ptr;
	return (b[3]<<24) | (b[2]<<16) | (b[1]<<8) | b[0];
}

void LE32W(void *ptr, u32 val)
{
	u8 *b = (u8*)ptr;
	b[0] = (val>> 0)&0xff;
	b[1] = (val>> 8)&0xff;
	b[2] = (val>>16)&0xff;
	b[3] = (val>>24)&0xff;
}


/**********************************************************/


u32 pad_read(void)
{
    u32 bits = 0;

	while(SF&0x01);
	SF = 0x01;
	COMREG = 0x10;
	while(SF&0x01);

	bits = (OREG2<<8) | (OREG3);
	IREG0 = 0x40;

    return bits ^ 0xFFFF;
}


void pad_init(void)
{
	DDR1 = 0;
	EXLE = 0;
    IOSEL = 0;

	IREG0 = 0x00;
	IREG1 = 0x0a;
	IREG2 = 0xf0;
}


/**********************************************************/

int smpc_cmd(int cmd)
{
	printk("SMPC_CMD(%02x) ...\n", cmd);
	// wait busy state
	while(SF&0x01);

	SF = 0x01;
	COMREG = cmd;
	while(SF&0x01);

	printk("    done. SS=%02x\n", SR);
	return SR;
}

/**********************************************************/


void stm32_puts(char *str)
{
	u32 stm32_base = 0x22820000;
	int i, len;

	len = strlen(str);
	LE32W((void*)(stm32_base+0), len);

	memcpy((void*)stm32_base+0x10, str, len+1);

	SS_CMD = 0x0001;
	while(SS_CMD);
}


/**********************************************************/

const int HZ = 1000000;

void reset_timer(void)
{
	SS_TIMER = 0;
}

u32 get_timer(void)
{
	return SS_TIMER;
}

void usleep(u32 us)
{
	u32 now, end;

	if(us==0)
		us = 1;
	end = us*(HZ/1000000);

	reset_timer();
	while(1){
		now = get_timer();
		if(now>=end)
			break;
	}
}

void msleep(u32 ms)
{
	u32 now, end;

	if(ms==0)
		ms = 1;
	end = ms*(HZ/1000);

	reset_timer();
	while(1){
		now = get_timer();
		if(now>=end)
			break;
	}
}

/**********************************************************/

// clk = 26874880
// bps = ((clk/32)/n)/(BRR+1)

void sci_init(void)
{
	SCR = 0x00;     /* set TE,RE to 0 */
	SMR = 0x00;     /* set SMR to 8n1 async mode */
//	BRR = 0x15;     /* set BRR to 9600 bps */
//	BRR = 0x01;     /* set BRR to 104980 bps */
	BRR = 0x00;     /* set BRR to 209960 bps */
	SCR = 0xcd;     /* set TIE RIE MPIE TEIE CKE1 CKE0 */
	SCR = 0x31;     /* set TE RE to 1 */

	printk_putc = sci_putc;
}

void sci_putc(int ch)
{
	while((SSR&0x80)==0);
	TDR = ch;
	SSR &= 0x7f;
}

int sci_getc(int timeout)
{
	int ch;

	timeout *= 1500;
	while(timeout){
		if(SSR&0x40){
			ch = RDR;
			SSR &= 0xbf;
			return ch;
		}
		timeout -= 1;
	}

	return -1;
}


/**********************************************************/

int read_file (char *name, int offset, int size, void *buf)
{
	int retv;

	LE32W((void*)0x22820000, offset);
	LE32W((void*)0x22820004, size);
	strcpy((void*)0x22820010, name);

	SS_ARG = 0;
	SS_CMD = SSCMD_FILERD;
	while(SS_CMD);

	retv = (signed short)SS_ARG;
	if(retv<0)
		return retv;

	size = LE32((void*)0x22820004);
	memcpy(buf, (void*)0x22820100, size);
	return size;
}


int write_file(char *name, int offset, int size, void *buf)
{
	int retv;

	LE32W((void*)0x22820000, offset);
	LE32W((void*)0x22820004, size);
	strcpy((void*)0x22820010, name);
	memcpy((void*)0x22820100, buf, size);

	SS_ARG = 0;
	SS_CMD = SSCMD_FILEWR;
	while(SS_CMD);

	retv = (signed short)SS_ARG;
	if(retv<0)
		return retv;

	size = LE32((void*)0x22820004);
	return size;
}


/**********************************************************/


static u16 mksum(u32 addr)
{
	return (addr>>16) + addr;
}


int mem_test(int size)
{
	u32 addr0 = 0x22400000;
	u32 addr1 = 0x22600000;
	u16 rd, sum;
	int i;
	
	for(i=0; i<size; i++){
		*(u16*)(addr0) = mksum(addr0);
		addr0 += 2;
		*(u16*)(addr1) = mksum(addr1);
		addr1 += 2;
	}

	for(i=0; i<size; i++){
		addr0 -= 2;
		sum = mksum(addr0);
		rd  = *(u16*)(addr0);
		if(sum!=rd){
			printk("Mismatch0 at %08x! read=%08x calc=%08x\n", addr0, rd, sum);
			return -1;
		}
		*(u16*)(addr0) = ~sum;

		addr1 -= 2;
		sum = mksum(addr1);
		rd  = *(u16*)(addr1);
		if(sum!=rd){
			printk("Mismatch1 at %08x! read=%08x calc=%08x\n", addr1, rd, sum);
			return -2;
		}
		*(u16*)(addr1) = ~sum;
	}

	for(i=0; i<size; i++){
		sum = ~mksum(addr0);
		rd  = *(u16*)(addr0);
		if(sum!=rd){
			printk("Mismatch2 at %08x! read=%08x calc=%08x\n", addr0, rd, sum);
			return -3;
		}
		addr0 += 2;

		sum = ~mksum(addr1);
		rd  = *(u16*)(addr1);
		if(sum!=rd){
			printk("Mismatch3 at %08x! read=%08x calc=%08x\n", addr1, rd, sum);
			return -4;
		}
		addr1 += 2;
	}

	return 0;
}


/**********************************************************/

static int total_disc, total_page, page;

MENU_DESC sel_menu;


static void fill_selmenu(void)
{
	int *disc_path = (int *)0x22800004;
	char *path_str = (char*)0x22800000;
	char tmp[128];
	int i;

	for(i=0; i<11; i++){
		int index = page*11+i;
		if(index>=total_disc)
			break;
		// /SAROO/ISO/xxxx/xxxx.cue
		char *path = path_str + LE32(&disc_path[index]);
		snprintf(tmp, 128, "%2d: %s", index, path+11);
		tmp[127] = 0;
		char *p = strchr(tmp, '/');
		if(p)
			*p = 0;

		add_menu_item(&sel_menu, tmp);
	}
	sprintf(sel_menu.title, "选择游戏(%d/%d)", page+1, total_page);
}


static void page_update(int up)
{
	MENU_DESC *menu = &sel_menu;

	menu->current = (up)? 10: 0;
	menu->num = 0;
	fill_selmenu();
	draw_menu_frame(menu);
}


static int sel_handle(int ctrl)
{
	MENU_DESC *menu = &sel_menu;

	total_disc = LE32((u8*)0x22800000);
	total_page = (total_disc+10)/11;

	if(BUTTON_DOWN(ctrl, PAD_UP)){
		if(menu->current>0){
			menu->current -= 1;
			menu_update(menu);
		}else if(page>0){
			page -= 1;
			page_update(1);
		}
	}else if(BUTTON_DOWN(ctrl, PAD_DOWN)){
		if(menu->current<(menu->num-1)){
			menu->current += 1;
			menu_update(menu);
		}else if((page+1)<total_page){
			page += 1;
			page_update(0);
		}
	}else if(BUTTON_DOWN(ctrl, PAD_LT)){
		page -= 1;
		if(page<0){
			page = total_page-1;
		}
		page_update(0);
	}else if(BUTTON_DOWN(ctrl, PAD_RT)){
		page += 1;
		if(page>=total_page){
			page = 0;
		}
		page_update(0);
	}else if(BUTTON_DOWN(ctrl, PAD_A)){
		int index = page*11 + menu->current;

		menu_status(menu, "游戏启动中......");

		SS_ARG = index;
		SS_CMD = SSCMD_LOADDISC;
		while(SS_CMD);

		int retv = bios_cd_cmd();
		if(retv){
			char buf[40];
			sprintf(buf, "游戏启动失败! %d", retv);
			menu_status(menu, buf);
		}
	}else if(BUTTON_DOWN(ctrl, PAD_Z)){
		int index = page*11 + menu->current;

		SS_ARG = index;
		SS_CMD = SSCMD_LOADDISC;
		while(SS_CMD);

		bios_run_cd_player();
	}else if(BUTTON_DOWN(ctrl, PAD_C)){
		return MENU_EXIT;
	}

	return 0;
}


void select_game(void)
{
	total_disc = LE32((u8*)0x22800000);
	total_page = (total_disc+10)/11;
	page = 0;

	memset(&sel_menu, 0, sizeof(sel_menu));
	sel_menu.handle = sel_handle;

	fill_selmenu();

	menu_run(&sel_menu);
}


/**********************************************************/


char *menu_str[4] = {
	"选择游戏",
	"系统CD播放器",
	"串口调试工具",
	"运行二进制文件",
};
char *update_str = "固件升级";
int update_index;

MENU_DESC main_menu;


int main_handle(int ctrl)
{
	int index = main_menu.current;

	if(menu_default(&main_menu, ctrl))
		return 0;

	if(!BUTTON_DOWN(ctrl, PAD_A))
		return 0;

	if(index==0){
		select_game();
		return MENU_RESTART;
	}else if(index==1){
		bios_run_cd_player();
		return MENU_RESTART;
	}else if(index==2){
		menu_status(&main_menu, NULL);
		return MENU_EXIT;
	}else if(index==3){
		menu_status(&main_menu, "TODO");
	}else if(index==update_index){
		menu_status(&main_menu, "升级中,请勿断电...");
		SS_ARG = 0;
		SS_CMD = SSCMD_UPDATE;
		while(SS_CMD);
		if(SS_ARG){
			menu_status(&main_menu, "升级失败!");
		}else{
			menu_status(&main_menu, "升级完成,请重新开机!");
		}
		while(1);
	}

	return 0;
}



int check_update(void)
{
	SS_ARG = 0;
	SS_CMD = SSCMD_CHECK;
	while(SS_CMD);

	printk("check_update: %02x\n", SS_ARG);
	return SS_ARG;
}


void menu_init(void)
{
	int i;

	memset(&main_menu, 0, sizeof(main_menu));
	sprintf(main_menu.title, "SAROO Boot Menu            V%02x0000", SS_VER&0xff);

	for(i=0; i<4; i++){
		add_menu_item(&main_menu, menu_str[i]);
	}
	if(check_update()){
		add_menu_item(&main_menu, update_str);
		update_index = i;
	}

	main_menu.handle = main_handle;

	menu_run(&main_menu);
}


int _main(void)
{
	conio_init();
	pad_init();

	printk("    SRAOOO start!\n");

	//ASR0 = 0x31101f00;
	//AREF = 0x00000013;

#if 1
	sci_init();
	printk("\n\nSAROOO Serial Monitor Start!\n");
#endif


	// CDBlock Off
	smpc_cmd(CDOFF);

	// SAROOO On
	SS_CTRL = (SAROO_EN | CS0_RAM4M);

	while(1){
		menu_init();
		sci_shell();
	}

	return 0;
}


