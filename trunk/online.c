/* ---------------------------------------------------- */
/* online.c        (各BBS人气统计机)		        */
/* ---------------------------------------------------- */
/* Author: hightman.bbs@bbs.dot66.net                   */
/*         lazy_lee@bigfoot.com                         */
/* Create: 2001/12/02                                   */
/* Update:                                              */
/* ---------------------------------------------------- */
/* Usage : gcc -o online online.c		        */
/* Syntax: online	                                */
/* ---------------------------------------------------- */

/* $Id$ */

#define BBSLIST		"bbslist.conf"
#define PLOTDATA	"plotdata"
#define SCRIPTFILE	"plotscript"
#define	MAXSITE		300		/* BBS站的最多个数 */
#define	TIMEOUT		40
#define TOP_NUM		10
#define GNUPLOT_37   // define this if you are using gnuplot<4.0

#define IAC     0xff
#define DONT    0xfe
#define DO      0xfd
#define WONT    0xfc
#define WILL    0xfb
#define SB      0xfa
#define BREAK   0xf9
#define SE      0xf8

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <arpa/inet.h>

typedef struct {
	char site[32];
	char domain[32];
	int online[31][24];
} DATA;

typedef struct {
	int night;
	int morning;
	int afternoon;
	int wholeday;
} DAYAVG;

typedef struct {
	char site[32];
	char domain[32];
	int month[35];
	int year[13];
	int week[8];
} STAT;

int fd;
time_t now;
struct tm timep,lastupdate;

/* ------------------------------------------------ */
/* Strip the ansi Code                              */
/* ------------------------------------------------ */
void StripAnsi(char *start,int *len, char isContinue) {
	register int ch;
	char *dst,*src;
	static char ansi;


	src = dst = start;
	if (!isContinue) ansi = 0;
	for(;src-start<*len;src++) {
		ch = *src;

		if (ch == 27) {
			ansi = 1;
    		}
		else if (ansi) {
			if (isalpha(ch))
				ansi = 0;
		}
		else {
			*dst++ = ch;
		}
	}
	*dst = '\0';
	*len = dst-start;
}


/* ------------------------------------------------ */
/* Get the nextString split by some chars	    */
/* ------------------------------------------------ */

int IsSplitChar(int ch) {
	return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
}

char *nextword(char **str) {
	char *head, *tail;
	int ch;

	head = *str;
	for (;;) {
		ch = *head;
		if (!ch) {
			*str = head;
			return head;
		}
		if (!IsSplitChar (ch))
			break;
		head++;
	}
	tail = head + 1;
	while ((ch = *tail)) {
		if (IsSplitChar (ch)) {
			*tail++ = '\0';
			break;
		}
		tail++;
	}
	*str = tail;
	return head;
}

int IsNum(int ch){
	return (ch >= '0' && ch <= '9');
}

void timeout(){
	close(fd);
}

int SendIAC(int socket, unsigned char cmd, unsigned char opt)
{
   unsigned char io_data[3];
   io_data[0] = IAC;
   io_data[1] = cmd;
   io_data[2] = opt;
   return(write(socket,io_data,3));
}

void ProcessIAC(int socket, unsigned char *buf, int buflen)
{
   	int cmd, opt, i;

	for(i=0;i<buflen-2;i++) {
		if (buf[i]!=IAC) continue;

		cmd = buf[++i];
      		if( cmd == IAC ) {i--; continue;}

		opt = buf[++i];

		if( cmd == WILL )
                SendIAC(socket, DONT, opt );
                else if( cmd == DO )
                {
                	SendIAC(socket, WONT, opt );
			SendIAC(socket, DONT, opt );
                }
                else if( cmd == DONT )
                        SendIAC(socket, WONT, opt );
   	}
}


int MyRead(int socket, unsigned char *buf, int maxlen) {
	int j;
	j = read(socket, buf, maxlen);
	if (j<=0) {

		return -1;
	}
	if (j>maxlen) j=maxlen;

	ProcessIAC(socket,buf,j);
	buf[maxlen]=0;
	return j;
}

int GetOnline(char *host, int port, char *tag, char *cmd) {
	int online, flag, rc, retry, base;
	struct sockaddr_in blah;
	unsigned char buf[2048], *ptr, tmpbuf[10];

	bzero((char*) &blah, sizeof(blah));
	blah.sin_family=AF_INET;
	blah.sin_addr.s_addr=inet_addr(host);
	blah.sin_port=htons(port);

	signal(SIGALRM, timeout);
	alarm(TIMEOUT);

	fd=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(connect(fd, (struct sockaddr*) &blah, 16)<0) {
   		return -1;
 	}


 	online = flag = 0;
 	ptr = NULL;

 	base=0;
	for(retry=0;retry<20;retry++) {
		bzero( buf + base, 2048 - base);
		if((rc=MyRead(fd, buf + base, 2047 - base)) < 0) break;

		StripAnsi((char *)(buf + base), &rc, retry==0?0:1);
		rc+=base;	/*Merge*/

		/*自动登陆*/
		if ((cmd[0])&&( (ptr = (void *)memmem(buf,rc, "login", 5)))) {
			if((ptr+20) > (buf+rc)) { /*login应该出现在比较靠后的位置*/
				char cmdbuf[20];
				if (strlen(cmd) > 17 ) cmd[17]=0;
				sprintf(cmdbuf, "%s\n", cmd);
				if (write(fd,cmdbuf,strlen(cmdbuf))<0) {
					ptr=NULL;
					break;
				}
			}
		}


		if((ptr = (void *)memmem(buf,rc, tag,strlen(tag)))){
			if ((ptr+30) < (buf+rc)) break;
		}
		else if(memmem(buf,rc, "请输入",strlen("请输入"))) break;

		/*shift the tail of the buffer to the head*/
		if (rc<=30) {
			base=rc;
			continue;
		}
		else {
			int i;
			base=30;
			for (i=0;i<base;i++) buf[i]=buf[rc-base+i];
		}
	}

	close(fd);

	if(ptr == NULL) {
		return -1;
	}

	flag = 0;
	bzero(tmpbuf, 10);

	while(*ptr) {
		if(IsNum(*ptr)) {
			tmpbuf[flag] = *ptr;
			flag++;
			if (flag>9) break;
		}
		else if(flag != 0) break;
		if (ptr>buf+2047) break;
		ptr++;
	}

	online = atoi((char *)tmpbuf);
	return online;
}

char* GetDataFilename()
{
	static char filename[16];

	strftime(filename,16,"%Y%m.dat",&timep);

	return filename;
}

int GetWeekNum(struct tm atime)
{
	char temp[8];
	int i;
	strftime(temp, 8, "%W", &atime);
	i = atoi(temp);
	if(atime.tm_wday==0) i++;
	return i;
}

void SaveOriginalData(DATA data[], int count)
{
	FILE *fout;
	fout = fopen(GetDataFilename(), "w");
	if (fout==NULL)
	{
		fprintf(stderr, "Can't save original data!\n");
		exit(-1);
	}
	fwrite(data, sizeof(DATA), count, fout);
	fclose(fout);
}

void SaveStatisticData(STAT stat[], int count)
{
	FILE *fout;
	fout = fopen("stat.dat", "w");
	if (fout==NULL)
	{
		fprintf(stderr, "Can't save original data!\n");
		exit(-1);
	}
	fwrite(stat, sizeof(STAT), count, fout);
	fwrite(&now, sizeof(time_t), 1, fout);
	fclose(fout);
}

void GenDayGraphData(DATA data[], int rank[])
{
	FILE *fout;
	int i,j, n;

	fout=fopen(PLOTDATA,"w+");
	if(fout==NULL){
		fprintf(stderr,"Can not creat temp data file!\n");
		exit(-1);
	}

	for(i=0;i<TOP_NUM;i++)
	{
		fprintf(fout,"#Day data NO.%d: %s, %s\n",i,data[rank[i]].domain,data[rank[i]].site);
		n=0;
		for(j=0;j<24;j++) {
			if(data[rank[i]].online[timep.tm_mday-1][j]>=0) {
				fprintf(fout,"%d %d\n",j,data[rank[i]].online[timep.tm_mday-1][j]);
				n++;
			}
		}
		if (n==0) fprintf(fout,"0 0\n");
		fprintf(fout,"\n\n");
	}

	fclose(fout);

	fout=fopen(SCRIPTFILE,"w+");
	if(fout==NULL){
		fprintf(stderr,"Can not creat plot script file!\n");
		exit(-1);
	}

#ifdef GNUPLOT_37
	fprintf(fout,"set terminal png color\n");
#else
	fprintf(fout,"set terminal png\n");
#endif
	fprintf(fout,"set output \"day.png\" \n");
	fprintf(fout,"set title \"Top Ten Sites Population (%d.%d.%d)\" \n", 1900+timep.tm_year, timep.tm_mon+1, timep.tm_mday);
	fprintf(fout,"set xlabel \"Time\" \n");
	fprintf(fout,"set ylabel \"Online Population\" \n");
	fprintf(fout,"set xr [0:23] \n");
	fprintf(fout,"set yr [0:20000] \n");
	fprintf(fout,"set key top left Left reverse \n");
	fprintf(fout,"set timestamp \n");
	fprintf(fout,"set grid \n");
	fprintf(fout,"plot ");

	for(i=0;i<TOP_NUM;i++)
	{
		fprintf(fout,"\"%s\" index %d title '%s' with lines %d", PLOTDATA,i,data[rank[i]].domain,i+1);
		if(i<TOP_NUM-1) fprintf(fout, ", \\\n");
		else fprintf(fout,"\n");
	}

	fclose(fout);

}

void GenWeekGraphData(STAT stat[], int rank[])
{
	FILE *fout;
	int i,j,n;

	fout=fopen(PLOTDATA,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append temp data file!\n");
		exit(-1);
	}

	for(i=0;i<TOP_NUM;i++)
	{
		n=0;
		fprintf(fout,"#Week data NO.%d: %s, %s\n",i,stat[rank[i]].domain,stat[rank[i]].site);
		for(j=0;j<7;j++){
			if(stat[rank[i]].week[j]>=0){
				fprintf(fout,"%d %d\n",j,stat[rank[i]].week[j]);
				n++;
			}
		}
		if (n==0) fprintf(fout,"0 0\n");
		fprintf(fout,"\n\n");
	}

	fclose(fout);

	fout=fopen(SCRIPTFILE,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append plot script file!\n");
		exit(-1);
	}

#ifdef GNUPLOT_37
	fprintf(fout,"set terminal png color\n");
#else
	fprintf(fout,"set terminal png\n");
#endif
	fprintf(fout,"set output \"week.png\" \n");
	fprintf(fout,"set title \"Top Ten Sites Population (No.%d Week, %d)\" \n", GetWeekNum(timep)+1, 1900+timep.tm_year);
	fprintf(fout,"set xlabel \"Day\" \n");
	fprintf(fout,"set ylabel \"Online Population\" \n");
	fprintf(fout,"set xr [0:6] \n");
	fprintf(fout,"set yr [0:20000] \n");
	fprintf(fout,"set xtics ('Sun' 0, 'Mon' 1, 'Tue' 2, 'Wed' 3, 'Thu' 4, 'Fri' 5, 'Sat' 6)\n");
	fprintf(fout,"set key top left Left reverse \n");
	fprintf(fout,"set timestamp \n");
	fprintf(fout,"set grid \n");
	fprintf(fout,"plot ");

	for(i=0;i<TOP_NUM;i++)
	{
		fprintf(fout,"\"%s\" index %d title '%s' with lines %d", PLOTDATA,i+TOP_NUM,stat[rank[i]].domain,i+1);
		if(i<TOP_NUM-1) fprintf(fout, ", \\\n");
		else fprintf(fout,"\n");
	}

	fclose(fout);

}

void GenMonthGraphData(STAT stat[], int rank[])
{
	FILE *fout;
	int i,j,n;

	fout=fopen(PLOTDATA,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append temp data file!\n");
		exit(-1);
	}

	for(i=0;i<TOP_NUM;i++)
	{
		n=0;
		fprintf(fout,"#Month data NO.%d: %s, %s\n",i,stat[rank[i]].domain,stat[rank[i]].site);
		for(j=0;j<31;j++) {
			if(stat[rank[i]].month[j]>=0) {
				fprintf(fout,"%d %d\n",j+1,stat[rank[i]].month[j]);
				n++;
			}
		}
		if (n==0) fprintf(fout,"0 0\n");
		fprintf(fout,"\n\n");
	}

	fclose(fout);


	fout=fopen(SCRIPTFILE,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append plot script file!\n");
		exit(-1);
	}

#ifdef GNUPLOT_37
	fprintf(fout,"set terminal png color\n");
#else
	fprintf(fout,"set terminal png\n");
#endif
	fprintf(fout,"set output \"month.png\" \n");
	fprintf(fout,"set title \"Top Ten Sites Population (%d.%d)\" \n",1900+timep.tm_year,timep.tm_mon+1);
	fprintf(fout,"set xlabel \"Date\" \n");
	fprintf(fout,"set ylabel \"Online Population\" \n");
	fprintf(fout,"set xr [1:31] \n");
	fprintf(fout,"set xtics autofreq\n");
	fprintf(fout,"set yr [0:20000] \n");
	fprintf(fout,"set key top left Left reverse \n");
	fprintf(fout,"set timestamp \n");
	fprintf(fout,"set grid \n");
	fprintf(fout,"plot ");

	for(i=0;i<TOP_NUM;i++)
	{
		fprintf(fout,"\"%s\" index %d title '%s' with lines %d", PLOTDATA,i+2*TOP_NUM,stat[rank[i]].domain,i+1);
		if(i<TOP_NUM-1) fprintf(fout, ", \\\n");
		else fprintf(fout,"\n");
	}

	fclose(fout);
}

void GenYearGraphData(STAT stat[], int rank[])
{
	FILE *fout;
	int i,j,n;

	fout=fopen(PLOTDATA,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append temp data file!\n");
		exit(-1);
	}

	for(i=0;i<TOP_NUM;i++)
	{
		n=0;
		fprintf(fout,"#Year data NO.%d: %s, %s\n",i,stat[rank[i]].domain,stat[rank[i]].site);
		for(j=0;j<12;j++){
			if(stat[rank[i]].year[j]>=0){
				fprintf(fout,"%d %d\n",j,stat[rank[i]].year[j]);
				n++;
			}
		}
		if (n==0) fprintf(fout,"0 0\n");
		fprintf(fout,"\n\n");
	}

	fclose(fout);

	fout=fopen(SCRIPTFILE,"a");
	if(fout==NULL){
		fprintf(stderr,"Can not append plot script file!\n");
		exit(-1);
	}

#ifdef GNUPLOT_37
	fprintf(fout,"set terminal png color\n");
#else
	fprintf(fout,"set terminal png\n");
#endif
	fprintf(fout,"set output \"year.png\" \n");
	fprintf(fout,"set title \"Top Ten Sites Population (%d)\" \n",1900+timep.tm_year);
	fprintf(fout,"set xlabel \"Month\" \n");
	fprintf(fout,"set ylabel \"Online Population\" \n");
	fprintf(fout,"set xr [0:11] \n");
	fprintf(fout,"set xtics ('Jan' 0, 'Feb' 1, 'Mar' 2, 'Apr' 3, 'May' 4, 'Jun' 5, 'Jul' 6, 'Aug' 7, 'Sep' 8, 'Oct' 9, 'Nov' 10, 'Dec' 11)\n");
	fprintf(fout,"set yr [0:20000] \n");
	fprintf(fout,"set key top left Left reverse \n");
	fprintf(fout,"set timestamp \n");
	fprintf(fout,"set grid \n");
	fprintf(fout,"plot ");

	for(i=0;i<TOP_NUM;i++)
	{
		fprintf(fout,"\"%s\" index %d title '%s' with lines %d", PLOTDATA,i+TOP_NUM*3,stat[rank[i]].domain,i+1);
		if(i<TOP_NUM-1) fprintf(fout, ", \\\n");
		else fprintf(fout,"\n");
	}

	fclose(fout);

}

void CalcDayAvg(DATA data[], DAYAVG dayavg[], int count, int total[]){
	int m,n,o,j,k;

	bzero(total,28*sizeof(int));
	for(j=0;j<count;j++) {
 		m=0; n=0; o=0;
 		dayavg[j].night=0;
 		dayavg[j].morning=0;
 		dayavg[j].afternoon=0;
 		for (k=0; k<8; k++) {
 			if (data[j].online[timep.tm_mday-1][k]>=0) {
 				m++;
 				dayavg[j].night+=data[j].online[timep.tm_mday-1][k];
				total[k]+=data[j].online[timep.tm_mday-1][k];
  			}
  			if (data[j].online[timep.tm_mday-1][k+8]>=0) {
  				n++;
  				dayavg[j].morning+=data[j].online[timep.tm_mday-1][k+8];
  				total[k+8]+=data[j].online[timep.tm_mday-1][k+8];
  			}
  			if (data[j].online[timep.tm_mday-1][k+16]>=0) {
  				o++;
  				dayavg[j].afternoon+=data[j].online[timep.tm_mday-1][k+16];
  				total[k+16]+=data[j].online[timep.tm_mday-1][k+16];
  			}
  		}
  		dayavg[j].wholeday=dayavg[j].afternoon+dayavg[j].morning+dayavg[j].night;
  		if (m!=0) dayavg[j].night/=m;
  		else dayavg[j].night=-1;
  		if (n!=0) dayavg[j].morning/=n;
  		else dayavg[j].morning=-1;
  		if (o!=0) dayavg[j].afternoon/=o;
  		else dayavg[j].afternoon=-1;
  		if (m+n+o!=0) dayavg[j].wholeday/=m+n+o;
  		else dayavg[j].wholeday=-1;
  		if(dayavg[j].night>=0) total[24]+=dayavg[j].night;
  		if(dayavg[j].morning>=0) total[25]+=dayavg[j].morning;
  		if(dayavg[j].afternoon>=0) total[26]+=dayavg[j].afternoon;
  		if(dayavg[j].wholeday>=0) total[27]+=dayavg[j].wholeday;
  	}
}

void GenDayRank(char *dayoutput,DATA data[],DAYAVG dayavg[], int count,int total[]){
	char rankflag[MAXSITE];
	int rank[MAXSITE];
	int j,k,maxonline;
	FILE *fout;

  	fout = fopen(dayoutput, "w");
  	if(fout == NULL){
     		fprintf(stderr, "Can't not write to %s !\n", dayoutput);
     		return;
  	}
  	fprintf(fout,"                -----=====本日各转信站在线人数统计=====-----\n");
  	fprintf(fout,"                     (http://online.cn-bbs.org/day.png)\n\n");

  	/*全天排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(dayavg[k].wholeday > maxonline && rankflag[k]==0){
  				maxonline=dayavg[k].wholeday;
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

  	fprintf(fout,"名次 转信站名称               地址                            平均人数\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"  %2d %-25s%-35s%5d\n",j+1,data[rank[j]].site,data[rank[j]].domain,dayavg[rank[j]].wholeday);
  	}

	GenDayGraphData(data,rank);

	/*前8小时排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(dayavg[k].night>maxonline && rankflag[k]==0){
  				maxonline=dayavg[k].night;
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n原始数据: \n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"BBS     \\    Time|    0     1     2     3     4     5     6     7 |SubAVG   AVG\n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-17.17s|",data[rank[j]].domain);
  		for(k=0;k<8;k++){
  			fprintf(fout,"%6d",data[rank[j]].online[timep.tm_mday-1][k]);
  		}
  		fprintf(fout,"|%6d%6d\n",dayavg[rank[j]].night,dayavg[rank[j]].wholeday);
  	}
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"      Total      |%6d%12d%12d%12d      |\n",total[0],total[2],total[4],total[6]);
  	fprintf(fout,"                 |%12d%12d%12d%12d|%6d%6d\n",total[1],total[3],total[5],total[7],total[24],total[26]);
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");

	/*中8小时排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(dayavg[k].morning>maxonline && rankflag[k]==0){
  				maxonline=dayavg[k].morning;
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"BBS     \\    Time|    8     9     10    11    12    13    14    15|SubAVG   AVG\n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-17.17s|",data[rank[j]].domain);
  		for(k=8;k<16;k++){
  			fprintf(fout,"%6d",data[rank[j]].online[timep.tm_mday-1][k]);
  		}
  		fprintf(fout,"|%6d%6d\n",dayavg[rank[j]].morning,dayavg[rank[j]].wholeday);
  	}
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"      Total      |%6d%12d%12d%12d      |\n",total[8],total[10],total[12],total[14]);
  	fprintf(fout,"                 |%12d%12d%12d%12d|%6d%6d\n",total[9],total[11],total[13],total[15],total[25],total[27]);
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n\n");

  	/*晚8小时排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(dayavg[k].afternoon>maxonline && rankflag[k]==0){
  				maxonline=dayavg[k].afternoon;
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"BBS     \\    Time|    16    17    18    19    20    21    22    23|SubAVG   AVG\n");
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-17.17s|",data[rank[j]].domain);
  		for(k=16;k<24;k++){
  			fprintf(fout,"%6d",data[rank[j]].online[timep.tm_mday-1][k]);
  		}
  		fprintf(fout,"|%6d%6d\n",dayavg[rank[j]].afternoon,dayavg[rank[j]].wholeday);
  	}
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n");
  	fprintf(fout,"      Total      |%6d%12d%12d%12d      |\n",total[16],total[18],total[20],total[22]);
  	fprintf(fout,"                 |%12d%12d%12d%12d|%6d%6d\n",total[17],total[19],total[21],total[23],total[26],total[27]);
  	fprintf(fout,"-----------------+------------------------------------------------+------------\n\n");

	fprintf(fout,"注:\n");
	fprintf(fout,"1. SubAVG 为 8 小时平均, AVG 为 24 小时平均;\n");
	fprintf(fout,"2.数据为 -1 表示因为某种原因无法采样,不纳入平均;\n");
	fprintf(fout,"3.网页版本位于 http://online.cn-bbs.org ,欲加入统计请至 http://cn-bbs.org 申请;\n");
	fprintf(fout,"4.Programmed by hightman<hightman@263.net> & Lazy<lazy_lee@bigfoot.com>.\n");
  	fclose(fout);
}


void CalcMonthAvg(STAT stat[], int count, int total[]){
	int m,n,o,j,k;

	bzero(total,35*sizeof(int));
	for(j=0;j<count;j++) {
 		m=0; n=0; o=0;
 		stat[j].month[31]=0;
 		stat[j].month[32]=0;
 		stat[j].month[33]=0;
 		for (k=0; k<10; k++) {
 			if (stat[j].month[k]>=0) {
 				m++;
 				stat[j].month[31]+=stat[j].month[k];
				total[k]+=stat[j].month[k];
  			}
  			if (stat[j].month[k+10]>=0) {
  				n++;
  				stat[j].month[32]+=stat[j].month[k+10];
  				total[k+10]+=stat[j].month[k+10];
  			}
  			if (stat[j].month[k+20]>=0) {
  				o++;
  				stat[j].month[33]+=stat[j].month[k+20];
  				total[k+20]+=stat[j].month[k+20];
  			}

  		}
  		if (stat[j].month[30]>=0) {
  			o++;
  			stat[j].month[33]+=stat[j].month[30];
  			total[30]+=stat[j].month[30];
   		}
   		stat[j].month[34]=stat[j].month[31]+stat[j].month[32]+stat[j].month[33];
  		if (m!=0) stat[j].month[31]/=m;
  		else stat[j].month[31]=-1;
  		if (n!=0) stat[j].month[32]/=n;
  		else stat[j].month[32]=-1;
  		if (o!=0) stat[j].month[33]/=o;
  		else stat[j].month[33]=-1;
  		if (m+n+o!=0) stat[j].month[34]/=m+n+o;
  		else stat[j].month[34]=-1;
  		if(stat[j].month[31]>=0) total[31]+=stat[j].month[31];
  		if(stat[j].month[32]>=0) total[32]+=stat[j].month[32];
  		if(stat[j].month[33]>=0) total[33]+=stat[j].month[33];
  		if(stat[j].month[34]>=0) total[34]+=stat[j].month[34];
  	}
}

void GenMonthRank(char *monthoutput,STAT stat[],int count,int total[]){
	char rankflag[MAXSITE];
	int rank[MAXSITE];
	int j,k,maxmonth;
	FILE *fout;

  	fout = fopen(monthoutput, "w");
  	if(fout == NULL){
     		fprintf(stderr, "Can't not write to %s !\n", monthoutput);
     		return;
  	}
  	fprintf(fout,"                -----=====本月各转信站在线人数统计=====-----\n");
  	fprintf(fout,"                    (http://online.cn-bbs.org/month.png)\n\n");

  	/*全月排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxmonth=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].month[34]>maxmonth && rankflag[k]==0){
  				maxmonth=stat[k].month[34];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

  	fprintf(fout,"名次 转信站名称               地址                            平均人数\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"  %2d %-25s%-35s%5d\n",j+1,stat[rank[j]].site,stat[rank[j]].domain,stat[rank[j]].month[34]);
  		/*printf("rank:%d, no:%d, domain:%s\n",j+1,rank[j],stat[rank[j]].domain);*/
  	}

	GenMonthGraphData(stat,rank);

	/*排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxmonth=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].month[31]>maxmonth && rankflag[k]==0){
  				maxmonth=stat[k].month[31];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n原始数据: \n");
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	fprintf(fout,"BBS      \\     Day |   1   2   3   4   5   6   7   8   9  10|SubAVG AVG \n");
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-19.19s|",stat[rank[j]].domain);
  		for(k=0;k<10;k++){
  			fprintf(fout,"%4d",stat[rank[j]].month[k]);
  		}
  		fprintf(fout,"| %4d %4d\n",stat[rank[j]].month[31],stat[rank[j]].month[34]);
  	}
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	fprintf(fout,"       Total       | %-8d%-8d%-8d%-8d%-7d|\n",total[0],total[2],total[4],total[6],total[8]);
  	fprintf(fout,"                   |%8d%8d%8d%8d%8d| %4d %4d\n",total[1],total[3],total[5],total[7],total[9],total[31],total[34]);
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");

	/*排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxmonth=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].month[32]>maxmonth && rankflag[k]==0){
  				maxmonth=stat[k].month[32];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n");
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	fprintf(fout,"BBS      \\     Day |  11  12  13  14  15  16  17  18  19  20|SubAVG AVG \n");
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-19.19s|",stat[rank[j]].domain);
  		for(k=10;k<20;k++){
  			fprintf(fout,"%4d",stat[rank[j]].month[k]);
  		}
  		fprintf(fout,"| %4d %4d\n",stat[rank[j]].month[32],stat[rank[j]].month[34]);
  	}
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");
  	fprintf(fout,"       Total       | %-8d%-8d%-8d%-8d%-7d|\n",total[10],total[12],total[14],total[16],total[18]);
  	fprintf(fout,"                   |%8d%8d%8d%8d%8d| %4d %4d\n",total[11],total[13],total[15],total[17],total[19],total[32],total[34]);
  	fprintf(fout,"-------------------+----------------------------------------+----------\n");

  	/*排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxmonth=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].month[33]>maxmonth && rankflag[k]==0){
  				maxmonth=stat[k].month[33];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n");
  	fprintf(fout,"-------------------+--------------------------------------------+----------\n");
  	fprintf(fout,"BBS      \\     Day |  21  22  23  24  25  26  27  28  29  30  31|SubAVG AVG \n");
  	fprintf(fout,"-------------------+--------------------------------------------+----------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-19.19s|",stat[rank[j]].domain);
  		for(k=20;k<31;k++){
  			fprintf(fout,"%4d",stat[rank[j]].month[k]);
  		}
  		fprintf(fout,"| %4d %4d\n",stat[rank[j]].month[33],stat[rank[j]].month[34]);
  	}
  	fprintf(fout,"-------------------+--------------------------------------------+----------\n");
  	fprintf(fout,"       Total       | %-8d%-8d%-6d%5d%8d%8d|\n",total[20],total[22],total[24],total[26],total[28],total[30]);
  	fprintf(fout,"                   |%8d%8d%8d%8d%8d    | %4d %4d\n",total[21],total[23],total[25],total[27],total[29],total[33],total[34]);
  	fprintf(fout,"-------------------+--------------------------------------------+----------\n\n");

	fprintf(fout,"注:\n");
	fprintf(fout,"1. SubAVG 为 10 天平均, AVG 为全月平均;\n");
	fprintf(fout,"2.数据为 -1 表示因为某种原因无法采样,不纳入平均;\n");
	fprintf(fout,"3.网页版本位于 http://month.cn-bbs.org ,欲加入统计请至 http://cn-bbs.org 申请;\n");
	fprintf(fout,"4.Programmed by hightman<hightman@263.net> & Lazy<lazy_lee@bigfoot.com>.\n");
  	fclose(fout);
}

void CalcYearAvg(STAT stat[], int count, int total[]){
	int m,j,k;

	bzero(total,13*sizeof(int));
	for(j=0;j<count;j++) {
 		m=0;
 		stat[j].year[12]=0;
 		for (k=0; k<12; k++) {
 			if (stat[j].year[k]>=0) {
 				m++;
 				stat[j].year[12]+=stat[j].year[k];
				total[k]+=stat[j].year[k];
  			}

  		}
    		if (m!=0) stat[j].year[12]/=m;
  		else stat[j].year[12]=-1;
  		if(stat[j].year[12]>=0) total[12]+=stat[j].year[12];
  	}
}

void GenYearRank(char *yearoutput,STAT stat[],int count,int total[]){
	char rankflag[MAXSITE];
	int rank[MAXSITE];
	int j,k,maxyear;
	FILE *fout;

  	fout = fopen(yearoutput, "w");
  	if(fout == NULL){
     		fprintf(stderr, "Can't not write to %s !\n", yearoutput);
     		return;
  	}
  	fprintf(fout,"                -----=====本年各转信站在线人数统计=====-----\n");
  	fprintf(fout,"                     (http://online.cn-bbs.org/year.png)\n\n");

  	/*全年排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxyear=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].year[12]>maxyear && rankflag[k]==0){
  				maxyear=stat[k].year[12];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

  	fprintf(fout,"名次 转信站名称               地址                            平均人数\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"  %2d %-25s%-35s%5d\n",j+1,stat[rank[j]].site,stat[rank[j]].domain,stat[rank[j]].year[12]);
  		/*printf("rank:%d, no:%d, domain:%s\n",j+1,rank[j],stat[rank[j]].domain);*/
  	}

	GenYearGraphData(stat,rank);

  	/*排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxyear=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].year[12]>maxyear && rankflag[k]==0){
  				maxyear=stat[k].year[12];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

	fprintf(fout,"\n原始数据: \n");
  	fprintf(fout,"-------------------+-------------------------------------------------+-------\n");
  	fprintf(fout,"BBS      \\    Month|   1   2   3   4   5   6   7   8   9  10  11  12 |  AVG\n");
  	fprintf(fout,"-------------------+-------------------------------------------------+-------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-19.19s|",stat[rank[j]].domain);
  		for(k=0;k<12;k++){
  			fprintf(fout,"%4d",stat[rank[j]].year[k]);
  		}
  		fprintf(fout," | %4d \n",stat[rank[j]].year[12]);
  	}
  	fprintf(fout,"-------------------+-------------------------------------------------+----------\n");
  	fprintf(fout,"       Total       | %-8d%-8d%-8d%-8d%-8d%-7d |\n",total[0],total[2],total[4],total[6],total[8],total[10]);
  	fprintf(fout,"                   |%9d%8d%8d%8d%8d%7d | %4d \n",total[1],total[3],total[5],total[7],total[9],total[11],total[12]);
  	fprintf(fout,"-------------------+-------------------------------------------------+----------\n");

	fprintf(fout,"注:\n");
	fprintf(fout,"1.数据为 -1 表示因为某种原因无法采样,不纳入平均;\n");
	fprintf(fout,"3.网页版本位于 http://year.cn-bbs.org ,欲加入统计请至 http://cn-bbs.org 申请;\n");
	fprintf(fout,"3.Programmed by hightman<hightman@123.net> & Lazy<lazy_lee@bigfoot.com>.\n");
  	fclose(fout);
}

void CalcWeekAvg(STAT stat[], int count, int total[]){
	int m,j,k;

	bzero(total,8*sizeof(int));
	for(j=0;j<count;j++) {
 		m=0;
 		stat[j].week[7]=0;
 		for (k=0; k<7; k++) {
 			if (stat[j].week[k]>=0) {
 				m++;
 				stat[j].week[7]+=stat[j].week[k];
				total[k]+=stat[j].week[k];
  			}
  		}
    	if (m!=0) stat[j].week[7]/=m;
  		else stat[j].week[7]=-1;
  		if(stat[j].week[7]>=0) total[7]+=stat[j].week[7];
  	}
}

void GenWeekRank(char *weekoutput,STAT stat[],int count,int total[]){
	char rankflag[MAXSITE];
	int rank[MAXSITE];
	int j,k,maxonline;
	FILE *fout;

  	fout = fopen(weekoutput, "w");
  	if(fout == NULL){
     		fprintf(stderr, "Can't not write to %s !\n", weekoutput);
     		return;
  	}
  	fprintf(fout,"                -----=====本周各转信站在线人数统计=====-----\n");
  	fprintf(fout,"                     (http://online.cn-bbs.org/week.png)\n\n");

  	/*全周排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].week[7]>maxonline && rankflag[k]==0){
  				maxonline=stat[k].week[7];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}

  	fprintf(fout,"名次 转信站名称               地址                            平均人数\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"  %2d %-25s%-35s%5d\n",j+1,stat[rank[j]].site,stat[rank[j]].domain,stat[rank[j]].week[7]);
  		/*printf("rank:%d, no:%d, domain:%s\n",j+1,rank[j],stat[rank[j]].domain);*/
  	}

	GenWeekGraphData(stat,rank);

  	/*排序*/
	bzero(rankflag,MAXSITE);
  	for(j=0;j<count;j++){
  		maxonline=-32767;
  		for(k=0;k<count;k++){
  			if(stat[k].week[7]>maxonline && rankflag[k]==0){
  				maxonline=stat[k].week[7];
  				rank[j]=k;
  			}
  		}
  		rankflag[rank[j]]=1;
  	}
	fprintf(fout,"\n原始数据: \n");
  	fprintf(fout,"-------------------+-------------------------------------------+-------\n");
  	fprintf(fout,"BBS      \\     Day |   Sun   Mon   Tue   Wed   Thu   Fri   Sat |  AVG\n");
  	fprintf(fout,"-------------------+-------------------------------------------+-------\n");
  	for(j=0;j<count;j++){
  		fprintf(fout,"%-19.19s|",stat[rank[j]].domain);
  		for(k=0;k<7;k++){
  			fprintf(fout,"%6d",stat[rank[j]].week[k]);
  		}
  		fprintf(fout," |%6d \n",stat[rank[j]].week[7]);
  	}
  	fprintf(fout,"-------------------+-------------------------------------------+-------\n");
  	fprintf(fout,"       Total       |%6d%12d%12d%12d |\n",total[0],total[2],total[4],total[6]);
  	fprintf(fout,"                   |%12d%12d%12d       |%6d\n",total[1],total[3],total[5],total[7]);
  	fprintf(fout,"-------------------+-------------------------------------------+-------\n\n");

	fprintf(fout,"注:\n");
	fprintf(fout,"1. AVG 为 7 天平均;\n");
	fprintf(fout,"2.数据为 -1 表示因为某种原因无法采样,不纳入平均;\n");
	fprintf(fout,"2.网页版本位于 http://online.cn-bbs.org ,欲加入统计请至 http://cn-bbs.org 申请;\n");
	fprintf(fout,"4.Programmed by hightman<hightman@123.net> & Lazy<lazy_lee@bigfoot.com>.\n");
  	fclose(fout);
}

void GetTime()
{
	now = time(NULL);
	localtime_r(&now,&timep);
}

int LoadOldOriginalData(DATA data[])
{
	FILE *fin;
	int len, i, rc;

	fin=fopen(GetDataFilename(),"rb");
	if (fin==NULL) return 0;

	fseek(fin,0,SEEK_END);
	len = ftell(fin);
	fseek(fin,0,SEEK_SET);

	if (len % sizeof(DATA)) {
		fprintf(stderr,"Original data file corrupt!\n");
		fclose(fin);
		exit(-1);
	}

	if (len !=0)
	{
		i = len / sizeof(DATA);
		if (i>MAXSITE) {
			i=MAXSITE;
			fprintf(stderr,"Warnning: Too many sites!\n");
		}
		rc = fread(data, sizeof(DATA), i, fin);
		if (i!=rc) {
			fprintf(stderr,"Read original data file error!\n");
			fclose(fin);
			exit(-1);
		}
	}
	else i=0;

	fclose(fin);
	return i;
}

int LoadOldStatisticData(char *filename, STAT stat[])
{
	FILE *fin;
	int len, i, rc, mod;
	time_t last;

	fin=fopen(filename,"rb");
	if (fin==NULL) return 0;

	fseek(fin,0,SEEK_END);
	len = ftell(fin);
	fseek(fin,0,SEEK_SET);

	mod = len % sizeof(STAT);
	if (mod!=0 && mod!=sizeof(time_t)) {
		fprintf(stderr,"Statistic file corrupt!\n");
		fclose(fin);
		exit(-1);
	}

	if (len !=0)
	{
		i = len / sizeof(STAT);
		if (i>MAXSITE) {
			i=MAXSITE;
			fprintf(stderr,"Warnning: Too many sites!\n");
		}
		rc = fread(stat, sizeof(STAT), i, fin);
		if (i!=rc) {
			fprintf(stderr,"Read statistic data file error!\n");
			fclose(fin);
			exit(-1);
		}

		if (mod==0) last=now;
		else fread(&last, sizeof(time_t), 1, fin);
		localtime_r(&last,&lastupdate);
	}
	else i=0;

	fclose(fin);
	return i;
}

void ClearOldStatData(STAT stat[], int count)
{
	int i,j;

	if (timep.tm_mon != lastupdate.tm_mon)
	{
		for(i=0;i<count;i++)
			for(j=0;j<35;j++)
				stat[i].month[j]=-1;
	}
	if (GetWeekNum(timep)!=GetWeekNum(lastupdate))
	{
		for(i=0;i<count;i++)
			for(j=0;j<8;j++)
				stat[i].week[j]=-1;
	}
	if (timep.tm_year != lastupdate.tm_year )
	{
		for(i=0;i<count;i++)
			for(j=0;j<13;j++)
				stat[i].year[j]=-1;
	}
}

int main(int argc, char *argv[]) {
	extern char *optarg;
	char buf[256], *ptr;
	char *domainName, *siteName, *bbsHost, *tagString, *loginCmd;
	int i,j, k, index, count, bbsPort, total[35],rows,nsite;
	time_t t1, t2;
	FILE *fin;
	DATA data[MAXSITE],temp;
	DAYAVG dayavg[MAXSITE];
	STAT stat[MAXSITE],tempstat;
	char cmd[32];
	
	GetTime();
	bzero(data,sizeof(DATA)*MAXSITE);
	rows=LoadOldOriginalData(data);

	fin = fopen(BBSLIST, "r");
	if(fin == NULL) {
		fprintf(stderr, "Can't not open the config file: %s!\n", BBSLIST);
		exit(-1);
	}

	/*
	   index为当前BBS站点在BBSLIST中的顺序号
	   i为当前BBS站点在旧的数据记录文件中的顺序号
	*/
	index = 0; nsite=0;
	while(fgets(buf, sizeof(buf), fin)) {
		if(buf[0] == '\n' || buf[0] == '\r' || buf[0] == '#') continue;
		nsite++;
		ptr = buf;
		domainName = nextword(&ptr);
		siteName   = nextword(&ptr);
		bbsHost    = nextword(&ptr);
		bbsPort    = atoi(nextword(&ptr));
		tagString  = nextword(&ptr);
		loginCmd   = nextword(&ptr);

		/*以域名为关键字*/
		if(!(strcmp(data[index].domain, domainName)))
		{
			i=index;
		}
		else
		{
			for(i=index+1; i < rows; i++) {
				if(!strcmp(data[i].domain, domainName)) break;
			}
			if (i>rows) i=rows;
			if (i==rows)
			{
				if (i>MAXSITE) {
					i=MAXSITE;
					fprintf(stderr,"Warnning: Too many sites!\n");
					break;
				}
				bzero(&data[i],sizeof(DATA));
				strncpy(data[i].site, siteName, sizeof(data[i].site));
				strncpy(data[i].domain, domainName, sizeof(data[i].domain));
				for(j=0;j<31;j++) {
					for (k=0;k<24;k++){
						data[i].online[j][k]=-1;
					}
				}
				rows++;
			}

			/*交换数据记录，以保证记录顺序与BBSLIST一致*/
			memcpy(&temp,&data[index],sizeof(data[index]));
			memcpy(&data[index],&data[i],sizeof(data[i]));
			memcpy(&data[i],&temp,sizeof(temp));
		}

		/*保证数据记录文件中的siteName与BBSLIST中的一致*/
		strncpy(data[index].site,siteName,31);

		t1 = time(NULL);
		count = GetOnline(bbsHost, bbsPort, tagString, loginCmd);
		t2 = time(NULL);
		printf("No.%d %s 当前在线(%2d时)：%d 人。", index+1, siteName, timep.tm_hour, count);
		if (count>=0) {
			printf("连接耗时：%d 秒。\n", (int)(t2-t1));
		} else {
			printf("\n");
		}
		data[index].online[timep.tm_mday-1][timep.tm_hour] = count;
		index++;
	}
	fclose(fin);

	SaveOriginalData(data,rows);

	CalcDayAvg(data,dayavg,index,total);
	GenDayRank("day",data,dayavg,index,total);

	rows = LoadOldStatisticData("stat.dat",stat);
	ClearOldStatData(stat,rows);

	for(index=0;index<nsite;index++)
	{
		/*以域名为关键字*/
		if(!(strcmp(stat[index].domain, data[index].domain)))
		{
			i=index;
		}
		else
		{
			for(i=index+1; i < rows; i++) {
				if(!strcmp(stat[i].domain, data[index].domain)) break;
			}
			if (i>rows) i=rows;
			if (i==rows)
			{
				if (i>MAXSITE) {
					i=MAXSITE;
					fprintf(stderr,"Warnning: Too many sites!\n");
					break;
				}
				bzero(&stat[i],sizeof(stat[i]));
				strcpy(stat[i].site, data[index].site);
				strcpy(stat[i].domain, data[index].domain);
				for(j=0;j<35;j++) stat[i].month[j]=-1;
				for(j=0;j<13;j++) stat[i].year[j]=-1;
				for(j=0;j<8;j++) stat[i].week[j]=-1;
				rows++;
			}

			/*交换数据记录，以保证记录顺序与BBSLIST一致*/
			memcpy(&tempstat,&stat[index],sizeof(stat[index]));
			memcpy(&stat[index],&stat[i],sizeof(stat[i]));
			memcpy(&stat[i],&tempstat,sizeof(tempstat));
		}

		/*保证数据记录文件中的siteName与BBSLIST中的一致*/
		strcpy(stat[index].site,data[index].site);

		stat[index].week[timep.tm_wday] = dayavg[index].wholeday;
		stat[index].month[timep.tm_mday-1] = dayavg[index].wholeday;
		stat[index].year[timep.tm_mon] = dayavg[index].wholeday;
	}

	CalcWeekAvg(stat,index,total);
	GenWeekRank("week",stat,index,total);

	CalcMonthAvg(stat,rows,total);
	GenMonthRank("month",stat,rows,total);

	CalcYearAvg(stat,rows,total);
	GenYearRank("year",stat,rows,total);

	SaveStatisticData(stat,rows);

	sprintf(cmd,"/usr/bin/gnuplot %s",SCRIPTFILE);
	system(cmd);

  	return 0;
}
