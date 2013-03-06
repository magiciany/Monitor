/*数据提取成功
 *能够生成sql语句
 *SSH连接模块化
 *Linux模块基本完成
 *Oracle连接成功
 *Oracle建表完成
 *Linux服务器检查及数据插入完成
 */
#include <iostream>
#include <fstream>
#include <libssh/libssh.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <iostream>
#include <errno.h>
#include <string>
#include <cstring>
#include <sstream>
#include <occi.h>
#include <occiData.h>
#include <occiControl.h>
#include <occiCommon.h>
#include <occiObjects.h>
#include <iomanip>
#include <time.h>
#include <pthread.h>

#define DB "localhost:1521/monitor"
#define DB_USER "monitor"
#define DB_PASSWD "123456"
#define T_HOSTS "t_hosts"
#define T_LINUX "t_linux"
#define T_DISK "t_disk"
#define T_ALARM "t_alarm"
#define MAXTHREAD 10
#define TEMPSQL "/tmp/monitor.sql"

using namespace oracle::occi;
using namespace std;

struct mydata
{
	string cache;
	string data[20];
	unsigned short lines;
	void setData()
	{
		int i;
		//		cout<<pthread_self()<<": In SetData"<<endl;
		//		cout<<"In SetData, Cache is "<<cache<<endl;
		string::size_type ps,pe;
		ps=0;

		//        cout<<pthread_self()<<": In SetData,Loop start"<<endl;
		for ( i=0; i<lines; i++ )
		{
			pe = cache.find ( '\n' );
			//			cout<<"possion end = "<<pe<<endl;
			data[i]=cache.substr ( ps,pe );
			//			cout<<"Line "<<i+1<<": "<<data[i]<<endl;
			ps = pe+1;
			pe = cache.find ( '\0' );
			cache=cache.substr ( ps,pe );
			ps = 0;
		}

		//		cout<<pthread_self()<<": setData Finished"<<endl;
	};
};


/* ip为被监控对象IP地址
 * app为应用类型
 * port为该应用使用的端口
 * user,passwd分别为用户名和密码
 * other1和other2为特殊字节，如数据库的SID等
 */
struct host
{
	unsigned short int port;
	string ip, app, user, passwd, other1, other2, date;
};

struct Disk
{
	unsigned int space_used;
	unsigned int space_total;
	unsigned short space_percent;
	unsigned long int iNode_used;
	unsigned long int iNode_total;
	unsigned short iNode_percent;
	string disk_vol;
};


//ORACLE连接参数
host ora_select ( int i )
{
	host ret;
	string user,passwd,db;
	Environment *env;
	Connection  *con;
	user = DB_USER;
	passwd = DB_PASSWD;
	db = DB;
	//	int i = 1;
	env = Environment::createEnvironment ( Environment::DEFAULT );
	con = env->createConnection ( user, passwd, db );
	//	cout<<"Successed connecting to oracle"<<endl;
	stringstream sql;
	sql<<"select ip,port,app,username,passwd, other1, other2 from "<<T_HOSTS<<" where id="<<i;
	//	cout<<sql.str()<<endl;
	Statement *stmt;
	stmt = con->createStatement ( sql.str() );
	ResultSet *rs = stmt->executeQuery();

	while ( rs->next() ==true )
	{
		ret.ip = rs->getString ( 1 );
		ret.port = rs->getInt ( 2 );
		ret.app = rs->getString ( 3 );
		ret.user = rs->getString ( 4 );
		ret.passwd = rs->getString ( 5 );
		ret.other1 = rs->getString ( 6 );
		ret.other2 = rs->getString ( 7 );
	}

	stmt->closeResultSet ( rs );
	env->terminateConnection ( con );
	Environment::terminateEnvironment ( env );
	//	cout<<"IP = "<<ip<<endl<<"Port = "<<po<<endl<<"APP = "<<app<<endl<<"User = "<<ur<<endl<<"Password = "<<pwd<<endl;
	return ret;
}


int ora_insert ( string sql )
{
	string user,passwd,db;
	Environment *env;
	Connection  *con;
	user = DB_USER;
	passwd = DB_PASSWD;
	db = DB;
	env = Environment::createEnvironment ( Environment::DEFAULT );
	con = env->createConnection ( user, passwd, db );
	//	cout<<"Successed connecting to oracle"<<endl;
	Statement *stmt;
	stmt = con->createStatement ( sql );
	//	ResultSet *rs = stmt->executeQuery();
	unsigned int rs = stmt->executeUpdate();
	cout<<pthread_self() <<" : ora_insert :"<<rs<<endl;
	con->commit();
	env->terminateConnection ( con );
	Environment::terminateEnvironment ( env );
	//	cout<<"IP = "<<ip<<endl<<"Port = "<<po<<endl<<"APP = "<<app<<endl<<"User = "<<ur<<endl<<"Password = "<<pwd<<endl;
	return rs;
}




int verify_knownhost ( ssh_session session )
{
	int state, hlen;
	unsigned char *hash ;
	string hexa;
	state = ssh_is_server_known ( session );
	//	cout<<"ssh_is_server_known "<<state<<endl;
	hlen = ssh_get_pubkey_hash ( session, &hash );

	//	cout<<"ssh_get_pubkey_hash "<<hlen<<endl;
	//	cout<<"Hash is :"<<hash<<endl<<endl;
	if ( hlen < 0 )
		return -1;

	switch ( state )
	{
	case SSH_SERVER_KNOWN_OK:
		//        cout<<"SSH_SERVER_KNOWN_OK"<<endl;
		break;

	default:
		hexa = ssh_get_hexa ( hash, hlen );
		//       cout<<"in else"<<endl;
		//      cout<<hexa<<endl;
		ssh_write_knownhost ( session ) ;
		//     cout<<"finished ssh_write_knownhost"<<endl;
	}

	free ( hash );
	return 0;
}

int exec_cmd ( ssh_session s, string cmd, struct mydata *d )
{
	ssh_channel channel;
	unsigned short rc,wcl;
	wcl=0;
	char buff[1000];
	unsigned int nbytes;
	/*建立ssh_channel*/
	//    cout<<"Ready to create channel.\n";
	channel = ssh_channel_new ( s );

	if ( channel == NULL )
		return SSH_ERROR;

	rc = ssh_channel_open_session ( channel );

	if ( rc != SSH_OK )
	{
		ssh_channel_free ( channel );
		return rc;
	}

	//	cout<<"channel create success."<<rc<<endl;
	//执行命令
	rc = ssh_channel_request_exec ( channel,cmd.c_str() );
	//    cout<<"Command Execute success."<<rc<<endl;
	//读取屏写
	nbytes = ssh_channel_read ( channel, buff, sizeof ( buff ), 0 );
	//	cout<<"nbytes is "<<nbytes<<endl;
	//计算行数
	unsigned int i;

	for ( i=0; i<nbytes; )
	{
		if ( buff[i]!='\0' )
		{
			if ( buff[i]=='\n' )
				wcl++;      //How many lines

			i++;
		}
		else
		{
			break;
		}
	}

	//处理数据
	//    cout<<"Ready convert char[] to string"<<endl;
	string buffer ( buff );
	buffer = buffer+'\n';
	buffer = buffer+'\0';
	d->cache=buffer;
	d->lines=wcl;
	ssh_channel_send_eof ( channel );
	ssh_channel_close ( channel );
	ssh_channel_free ( channel );
	return 0;
}


class Linux
{
	private:
		string IP;
		unsigned short cpu_usage;
		unsigned short wc;    //line count of df
		unsigned int memory_used;
		unsigned int memory_total;
		unsigned short memory_percent;
		unsigned int defunct;
		//		unsigned long Date;
		string Date;
		//		stringstream disk_id;
		Disk *disk;
		stringstream linuxsql;//="insert into ";
	public:
		Linux ( string , string , unsigned );
		~Linux()
		{
			delete [] disk;
		};
		int setCPU ( struct mydata );
		int setDiskSpace ( struct mydata );
		int setDiskNode ( struct mydata ); //disk_vol iNode_used iNode_total
		int diskSQL ();
		int setMem ( struct mydata ); //memory_used memory_total
		int setDefun ( struct mydata ); //defunct
		string SQL();  //create insert SQL
		int showAll()
		{
			cout<<IP<<"\t"<<Date<<endl;
			return 0;
		};
};
inline int Linux::diskSQL()
{
	stringstream disksql,alarmsql;
	int i;
	int k;
	ofstream sqlfile;
	sqlfile.open ( TEMPSQL,ios::app );

	for ( i=0; i<wc; i++ )
	{
		disksql.str ( "" );
		disksql<<"insert into "<<T_DISK<<" VALUES (\'"\
		       <<disk[i].disk_vol<<"\' , "<<disk[i].space_used<<" , "<<disk[i].space_total<<" , "<<disk[i].space_percent\
		       <<" , "<<disk[i].iNode_used<<" , "<<disk[i].iNode_total<<" , "<<disk[i].iNode_percent<<" , \'"\
		       <<IP<<"\' , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'))";
		//		cout<<pthread_self()<<" : "<<disksql.str() <<endl;
		//		k = ora_insert ( disksql.str() );
		sqlfile<<disksql.str() <<endl;

		//        cout<<pthread_self()<<" : "<<k<<endl;
		if ( disk[i].space_percent > 80 )
		{
			alarmsql.str ( "" );
			alarmsql<<"insert into "<<T_ALARM<<" VALUES (\'"<<IP<<"\' , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'), \'Disk Space\' , \'"\
			        <<disk[i].disk_vol<<"\')";
			//			cout<<alarmsql<<endl;
			//			ora_insert ( alarmsql.str() );
			sqlfile<<alarmsql.str() <<endl;
		}
		else if ( disk[i].iNode_percent>80 )
		{
			alarmsql.str ( "" );
			alarmsql<<"insert into "<<T_ALARM<<" VALUES (\'"<<IP<<"\' , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'), \'Disk iNode\' , \'"\
			        <<disk[i].disk_vol<<"\')";
			//			cout<<alarmsql<<endl;
			//			ora_insert ( alarmsql.str() );
			sqlfile<<alarmsql.str() <<endl;
		}
	}

	sqlfile.close();
	return 0;
}
//inline Linux::Linux ( string i, unsigned long d, unsigned w )
inline Linux::Linux ( string i, string d,unsigned w )
{
	IP=i;
	Date=d;
	//	sleep ( 1 ); //由于随机数与时间有关，暂停1秒保证数字随机
	//	srand ( ( unsigned ) time ( 0 ) );
	//	disk_id<<Date<<rand() %10000<<endl;
	//    cout<<disk_id.str();
	wc=w;
	disk = new Disk[wc];
	wc--;
};

inline string Linux::SQL()
{
	//	int i = 0;
	//disk_id.str();
	//    linuxsql = linuxsql + "DISKTABLE" + " VALUES (\"" + IP + "\",\"" + disk[i].disk_vol + "\" , " + itoa(disk[i].space_used) ;//+ " , " + disk[i].space_total + " , " + disk[i].iNode_used + " , " + disk[i].iNode_total + " , " + memory_used + " , " + memory_total + " , " + defunct + " , " + Date + ");";
	linuxsql<<"insert into "<<T_LINUX<<" VALUES(\'"<<IP<<"\' , "<<cpu_usage\
	        << " , "<<memory_used<<" , "<<memory_total<<" , "<<memory_percent<<" , "<<defunct\
	        <<" , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'))";
	stringstream alarmsql;

	//    linuxsql<<T_LINUX<<" VALUES (\""<<IP<<"\" , "<<cpu_usage<<" , \""<<disk[i].disk_vol<<"\" , "<<disk[i].space_used <<" , "<<disk[i].space_total<<" , "<<disk[i].iNode_used<<" , "<<disk[i].iNode_total<<" , "<<memory_used<<" , "<<memory_total<<" , "<<defunct<<" , "<<Date<<");";
	//    cout <<"from linux::sql() "<<linuxsql.str() << endl;
	if ( cpu_usage > 80 )
	{
		alarmsql.str ( "" );
		alarmsql<<"insert into "<<T_ALARM<<" VALUES (\'"<<IP<<"\' , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'), \'CPU Usage\' , \'"\
		        <<cpu_usage<<"\')";
		//		cout<<alarmsql<<endl;
		ora_insert ( alarmsql.str() );
	}
	else if ( memory_percent>80 )
	{
		alarmsql.str ( "" );
		alarmsql<<"insert into "<<T_ALARM<<" VALUES (\'"<<IP<<"\' , TO_DATE(\'"<<Date<<"\', \'YYYY/MM/DD HH24:MI\'), \'Memory Usage\' , \'"\
		        <<memory_percent<<"\')";
		//		cout<<alarmsql<<endl;
		ora_insert ( alarmsql.str() );
	}

	return linuxsql.str();
};

inline int Linux::setDiskSpace ( struct mydata d )
{
	//    cout<<"In setDiskSpace()"<<endl;
	wc=d.lines;
	//    cout<<wc<<endl;
	int i;
	string::size_type ps,pe;
	ps=0;

	//    cout<<endl<<d.data[3]<<endl;
	for ( i=0; i<wc; i++ )
	{
		//        cout<<"i = "<<i<<"\twc = "<<wc<<endl;
		//disk_vol
		pe = d.data[i].find ( '\t' );
		disk[i].disk_vol = d.data[i].substr ( ps,pe );
		//        cout<<"disk_vol =  "<<disk[i].disk_vol<<endl;
		ps = pe+1;
		pe = d.data[i].find ( '\0' );
		d.data[i]=d.data[i].substr ( ps,pe );
		//space_used
		ps = 0;
		pe = d.data[i].find ( '\t' );
		disk[i].space_used = atoi ( d.data[i].substr ( ps,pe ).c_str() );
		//        cout<<"space_used =  "<<disk[i].space_used<<endl;
		ps = pe+1;
		pe = d.data[i].find ( '\0' );
		d.data[i]=d.data[i].substr ( ps,pe );
		//space_total=space_used+space_unused
		ps = 0;
		pe = d.data[i].find ( '\n' );
		disk[i].space_total = atoi ( d.data[i].substr ( ps,pe ).c_str() );
		disk[i].space_total += disk[i].space_used;
		//        cout<<"space_total =  "<<disk[i].space_total<<endl;
		disk[i].space_percent = disk[i].space_used*100/disk[i].space_total;
		//        cout<<"space used "<<disk[i].space_percent<<"%"<<endl;
	}

	return 0;
}


inline int Linux::setDiskNode ( struct mydata d )
{
	//    cout<<"In setDiskNode()"<<endl;
	int i;
	string::size_type ps,pe;
	ps=0;

	//    cout<<endl<<d.data[3]<<endl;
	/*TODO:数据查找，以免错误*/
	for ( i=0; i<wc; i++ )
	{
		//        cout<<"i = "<<i<<"\twc = "<<wc<<endl;
		//disk_vol
		pe = d.data[i].find ( '\t' );
		/*        disk[i].disk_vol = d.data[i].substr(ps,pe);
		        cout<<"disk_vol =  "<<disk[i].disk_vol<<endl;*/
		ps = pe+1;
		pe = d.data[i].find ( '\0' );
		d.data[i]=d.data[i].substr ( ps,pe );
		//space_used
		ps = 0;
		pe = d.data[i].find ( '\t' );
		disk[i].iNode_used = atoi ( d.data[i].substr ( ps,pe ).c_str() );
		//        cout<<"iNode_used =  "<<disk[i].iNode_used<<endl;
		ps = pe+1;
		pe = d.data[i].find ( '\0' );
		d.data[i]=d.data[i].substr ( ps,pe );
		//space_total=space_used+space_unused
		ps = 0;
		pe = d.data[i].find ( '\n' );
		disk[i].iNode_total = atoi ( d.data[i].substr ( ps,pe ).c_str() );
		//        disk[i].iNode_total += disk[i].iNode_used;
		//        cout<<"iNode_total =  "<<disk[i].iNode_total<<endl;
		disk[i].iNode_percent = disk[i].iNode_used*100/disk[i].iNode_total;
		//        cout<<disk[i].iNode_percent<<endl;
	}

	//    cout<<"exit from setiNode()"<<endl;
	return 0;
}

inline int Linux::setMem ( struct mydata d )
{
	//    cout<<"In setMem()"<<endl;
	int i;
	string::size_type ps,pe;
	ps=0;
	memory_used=0;
	memory_total=0;

	/*TODO:数据查找，以免错误*/
	for ( i=0; i<d.lines; i++ )
	{
		//        cout<<d.data[i]<<endl;
		//Memory Used
		ps = 0;
		pe = d.data[i].find ( '\t' );
		//        cout<<ps<<"\t"<<pe<<endl;
		memory_used += atoi ( d.data[i].substr ( ps,pe ).c_str() );
		//		cout<<memory_used<<endl;
		//        cout<<"Memory_used =  "<<memory_used<<endl;
		//Memory Total
		ps = pe + 1;
		pe = d.data[i].find ( '\n' );
		//        cout<<ps<<"\t"<<pe<<endl;
		d.data[i]=d.data[i].substr ( ps,pe );
		//        cout<<d.data[i];
		memory_total+=atoi ( d.data[i].c_str() );
		//		cout<<memory_total<<endl;
	}

	memory_percent=memory_used*100/memory_total;
	return 0;
}

inline int Linux::setCPU ( struct mydata d )
{
	//    cout<<"In setCPU()"<<endl;
	cpu_usage=100 - atoi ( d.data[d.lines-1].c_str() );
	//    cout<<"CPU: "<<cpu_usage<<endl;
	return 0;
}

inline int Linux::setDefun ( struct mydata d )
{
	//    cout<<"In setDefun()"<<endl;
	defunct = atoi ( d.data[d.lines-1].c_str() );
	//    cout<<"Defunct: "<<defunct<<endl;
	return 0;
}
ssh_session ssh_connect ( string h, unsigned short p, string u, string pwd )
{
	ssh_session my_ssh_session = ssh_new();
	int rc;
	ssh_options_set ( my_ssh_session, SSH_OPTIONS_HOST, h.c_str() );
	ssh_options_set ( my_ssh_session, SSH_OPTIONS_USER,u.c_str() );
	ssh_options_set ( my_ssh_session, SSH_OPTIONS_PORT, &p );
	rc = ssh_connect ( my_ssh_session );

	if ( rc != SSH_OK )
	{
		fprintf ( stderr, "Error connecting to localhost: %s\n", ssh_get_error ( my_ssh_session ) );
		exit ( -1 );
	}

	rc = verify_knownhost ( my_ssh_session );
	//	cout<<"verify_knownhost return code is "<<rc<<endl;
	rc = ssh_userauth_password ( my_ssh_session,NULL,pwd.c_str() );
	//	cout<<"ssh_userauth_password return code is "<<rc<<endl;
	return my_ssh_session;
}




//int setlinux ( host h, string dt )
void *setlinux ( void *h )
{
	ofstream sqlfile;
	sqlfile.open ( TEMPSQL,ios::app );
	struct host* hosts = ( struct host* ) h;
	struct host lh;
	lh.ip=hosts->ip;
	lh.app=hosts->app;
	lh.port=hosts->port;
	lh.user=hosts->user;
	lh.passwd=hosts->passwd;
	lh.date=hosts->date;
	lh.other1=hosts->other1;
	lh.other2=hosts->other2;
	//    cout<<lh.ip<<"\t"<<lh.port<<endl;
	cout<<pthread_self() <<" : "<<lh.ip<<endl;
	struct mydata fanhui;
	//    int rc;
	string command;
	/*建立ssh_session*/
	ssh_session mySession;
	mySession = ssh_connect ( lh.ip, lh.port, lh.user, lh.passwd );
	//获取磁盘空间
	//cout<<"Getting Disk Space Usage"<<endl;
	//cout<<"ready to Get disk space"<<endl;
	command="df -Pm | grep -v sed | grep -v /dev/shm | awk \'{print $6,$3,$4}\' OFS=\"\\t\" ORS=\"\\n\" ";
	exec_cmd ( mySession, command, &fanhui );
	//	cout<<pthread_self()<<" : df"<<endl;
	//cout<<"Command Execute Success"<<endl;
	//cout<<"Space, fanhui.cache is "<<endl<<fanhui.cache<<"fanhui.lines is "<<fanhui.lines<<endl<<endl;
	Linux aaa ( lh.ip,lh.date, fanhui.lines );
	fanhui.setData();
	//cout<<"to set disk space "<<endl;
	int i;
	aaa.setDiskSpace ( fanhui );
	//for (i=0;i<40;i++)
	//   cout<<"==";
	//cout<<endl;
	//获取iNode情况
	//cout<<"to get disk iNode"<<endl;
	//cout<<"Getting iNode Usage"<<endl;
	command = "df -Pi | grep -v nod | grep -v /dev/shm | awk \'{print $6,$3,$2}\' OFS=\"\\t\" ORS=\"\\n\" ";
	exec_cmd ( mySession, command, &fanhui );
	//	cout<<pthread_self()<<" : fanhui.lines is "<<fanhui.lines<<endl;
	//    cout<<pthread_self()<<" : df-i"<<endl;
	//cout<<"iNode, fanhui.cache is "<<endl<<fanhui.cache<<"fanhui.lines is "<<fanhui.lines<<endl<<endl;
	fanhui.setData();
	aaa.setDiskNode ( fanhui );
	//for (i = 0;i<40;i++)
	//   cout<<"==";
	//cout<<endl;
	//获取CPU使用率
	//cout<<"Getting CPU Usage"<<endl;
	command = "vmstat | awk \'NR==3  { print $15 }\' ";
	exec_cmd ( mySession, command , &fanhui );
	//	cout<<pthread_self()<<" : vmstat"<<endl;
	//cout<<"CPU, fanhui.cache is "<<endl<<fanhui.cache<<"fanhui.lines is "<<fanhui.lines<<endl<<endl;
	fanhui.setData();
	aaa.setCPU ( fanhui );
	//for (i = 0;i<40;i++)
	//  cout<<"==";
	//cout<<endl;
	//获取内存情况
	//cout<<"Getting Memory Usage"<<endl;
	command = "free -m | grep [MS] | awk \'{ print $3,$2 }\' OFS=\"\\t\" ORS=\"\\n\" ";
	exec_cmd ( mySession, command, &fanhui );
	//	cout<<pthread_self()<<" : free"<<endl;
	//cout<<"Memory, fanhui.cache is "<<endl<<fanhui.cache<<"fanhui.lines is "<<fanhui.lines<<endl<<endl;
	fanhui.setData();
	aaa.setMem ( fanhui );
	//   for (i = 0;i<40;i++)
	//       cout<<"==";
	//   cout<<endl;
	//僵死进程数
	//	cout<<"Getting Defunct Process"<<endl;
	command = "ps -ef |grep defun | grep -v grep | wc -l";
	exec_cmd ( mySession, command, &fanhui );
	//    cout<<pthread_self()<<" : ps"<<endl;
	//    cout<<"Defunct, fanhui.cache is "<<endl<<fanhui.cache<<"fanhui.lines is "<<fanhui.lines<<endl<<endl;
	fanhui.setData();
	aaa.setDefun ( fanhui );
	//   cout <<endl<<"Gen SQL "<<endl;
	//	cout<<pthread_self()<<" : Creating diskSQL"<<endl;
	aaa.diskSQL();
	//	cout<<pthread_self()<<" : diskSQL Created"<<endl;
	//	cout<<pthread_self()<<" : Creating LinuxSQL"<<endl;
	string sql = aaa.SQL();
	/*	long pos;
		pos=sqlfile.tellp();
		sqlfile.seekp(pos);
	    sqlfile.write(sql.c_str(),sql.length());*/
	sqlfile<<sql<<endl;
	//    sqlfile<<"hello"<<endl;
	sqlfile.close();
	//	cout<<"from setLinux \n\t"<<pthread_self()<<endl<<sql<<endl;
	//    cout<<pthread_self()<<" : insert into oracle"<<endl;
	//	ora_insert ( sql );
	ssh_disconnect ( mySession );
	ssh_free ( mySession );
	//	return 0;
}



int main()
{
	struct tm *mytime;
	time_t t;
	t=time ( NULL );
	mytime=localtime ( &t );
	stringstream dt;
	system ( ">/tmp/monitor.sql" );
	dt<<mytime->tm_year+1900<<"/"<<mytime->tm_mon+1<<"/"<<mytime->tm_mday<<" "<<mytime->tm_hour<<":"<<mytime->tm_min;
	//	unsigned long dt;
	//	dt= ( mytime->tm_year+1900 ) *1000000+ ( mytime->tm_mon+1 ) *10000+mytime->tm_mday*100+mytime->tm_hour;
	//    cout<<mytime->tm_year+1900<<mytime->tm_mon+1<<mytime->tm_mday<<mytime->tm_hour<<endl;
	//    cout<<"Date is "<<dt<<endl;
	/*TODO: 查询数据库，确定被访问对象类别，如果是linux服务器，则使用setlinux();*/
	int i = 0;
	host hosts;
	pthread_t tids[MAXTHREAD];
	int j[MAXTHREAD];

	do
	{
		j[i]=i;
		i++;
		hosts = ora_select ( i );
		hosts.date = dt.str();

		//        cout <<hosts.ip<<endl;
		if ( hosts.ip.empty() )
			break;

		if ( hosts.app == "linux-ssh" )
		{
			pthread_create ( &tids[i],NULL,setlinux, ( void* ) &hosts );
			//            setlinux ( hosts,dt.str() );
		}
		else if ( hosts.app == "oracle" )
			//TODO: Oracle检测脚本
			;
	}
	while ( 1 );
	pthread_exit ( NULL );
	ifstream sqlout( "/tmp/monitor.sql" );
	while ( !sqlout.eof() )
    {
        getline(sqlout,a);
        ora_insert(a);
    }
    sqlout.close();
	return 0;
}


