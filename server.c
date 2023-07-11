#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LENTYPE 4 //Lunghezza del tipo di messaggio, ad es: SIG\0 LOG\0 ACK\0 MSG\0 ecc

struct onlineDevices{       //serve per memorizzare gli utenti online
    char user_dest[1024];   //username
    int port;               //porta di ascolto dell'utente
    int socket;             //socket sul quale il server comunica con il client
    time_t time;            // timestamp login
};

struct bufferMsg{           //buffer dei messaggi che non è stato possibile recapitare
    char user_dest[1024];   //destinatario
    char user_src[1024];    //mittente
    uint32_t latest_time;   //timestamp del più recente
    char msg[20][1024];     //20 max 20 messaggi
    time_t time[20];       //per ogni messaggio devo salvare l'orario
    int lastIndexMsg;       //ultimo messaggio per l'utente destinatario da quel mittente
};

struct notificaConsegna{        //struttura che memorizza quale utente è da notificare dell'avvenuta consegna dei messaggi
    char utenteDaNotificare[1024];      //chi devo avvisare, perchè offline ora
    char utenteConsegnati[1024];        //a chi sonoo stati consegnati i messaggi
};

struct notificaConsegna daNotificare[10];
int lastUtenteDaNotificare=-1;

struct onlineDevices online[10];   //utenti online
int indexLastOnlineDevices=-1;

struct bufferMsg bufferMsgs[15];   
int indexLastBuffer=-1;

void inviaStringa(int socket,const char* stringa, uint32_t size){
    send(socket,(void*)&size,sizeof(uint32_t),0);
    send(socket,(void*)stringa,size,0);
}

void notificaMessaggi(int sd, const char* user){
    // devo avvisare l'utente della consegna dei messaggi
    int i=0;
    while(i<=lastUtenteDaNotificare){   //cerco se ho consegnato messaggi per conto dell'utente
        if(strcmp(daNotificare[i].utenteDaNotificare,user)==0){ 
            inviaStringa(sd,"DLV\0",LENTYPE);
            inviaStringa(sd,daNotificare[i].utenteConsegnati,strlen(daNotificare[i].utenteConsegnati));
            //ho appena inviato a chi ho inoltrato i messaggi
            daNotificare[i]=daNotificare[lastUtenteDaNotificare--]; //l'ultimo della lista prende il posto i-esimo, non incremento altrimenti j altrimenti non controllerei tutti
        }else
            i++;
    }
}

int is_online(const char *dest, int* pport,int *socket,int lunghezza){
    //conotrollo se l'utente dest è online, se si restituisco porta soceket e come return value 1
    int i;
    for(i=0; i<=indexLastOnlineDevices; i++){
        if(strncmp(online[i].user_dest,dest,lunghezza)==0){   
            *pport=online[i].port;
            *socket=online[i].socket;
            return 1;
        }
    }
    return 0;
}

void forwardMessages(int sd ,const char* dest,const char* src){
    //inoltro i messaggi, risposta ad operazione SHW (Show message)
    int i,j;
    uint32_t messageNumber=0;
    uint32_t length;
    int socket;

    for(i=0; i<=indexLastBuffer; i++){  //scorro l'array dei messaggi bufferizzati
        if(strcmp(dest, bufferMsgs[i].user_dest)==0 && strcmp(src,bufferMsgs[i].user_src)==0){ //ho trovato un insieme di messaggi per l'utente
           // mando numero di messaggi presenti per l'utente
           messageNumber = (uint32_t)(bufferMsgs[i].lastIndexMsg+1);
           send(sd,(void*)&messageNumber,sizeof(uint32_t),0);
           for(j=0; j<=bufferMsgs[i].lastIndexMsg; j++){
               //mando tutti i messaggi
                length= strlen(bufferMsgs[i].msg[j])+1;
                inviaStringa(sd,bufferMsgs[i].msg[j], length);
                //per ogni messaggio mando anche l'orario
                send(sd,(void*)&bufferMsgs[i].time[j],sizeof(time_t),0);
           }
           // ho mandato tutti i messaggi, devo ricompattare array
           if(indexLastBuffer!=0){  //ricompatto, altrimenti basta indice a -1 e poi sovrascrivo ogni volta
                strcpy(bufferMsgs[i].user_dest, bufferMsgs[indexLastBuffer].user_dest);
                strcpy(bufferMsgs[i].user_src, bufferMsgs[indexLastBuffer].user_src);
                bufferMsgs[i].latest_time=bufferMsgs[indexLastBuffer].latest_time;
                for(j=0;j<=bufferMsgs[indexLastBuffer].lastIndexMsg;j++){
                    strcpy( bufferMsgs[i].msg[j], bufferMsgs[indexLastBuffer].msg[j]);
                    bufferMsgs[i].time[j] = bufferMsgs[indexLastBuffer].time[j];
                }
                bufferMsgs[i].lastIndexMsg=bufferMsgs[indexLastBuffer].lastIndexMsg;
           }
           indexLastBuffer--;
           //devo notificare all'utente mittente che sono stati consegnati tutti i messaggi pendenti
           if(is_online(src,&j,&socket,strlen(src))!=0){    //se online
            //la porta non mi interessa (j), mi interessa il socket
               //mando DLV\0 Delivery to User
                inviaStringa(socket,"DLV\0",LENTYPE);
                //mando nome utente
                length=strlen(dest)+1;
                inviaStringa(socket,dest,length);
           }else{
               //utente offline, salvo in un buffer e notificherò al prossimo login
                lastUtenteDaNotificare++;
                strcpy(daNotificare[lastUtenteDaNotificare].utenteDaNotificare,src);
                strcpy(daNotificare[lastUtenteDaNotificare].utenteConsegnati,dest);
           }
           return;
        }
    }
    //se sono arrivato qui non ci sono messaggi per l'utente, mando 0
    send(sd,(void*)&messageNumber,sizeof(uint32_t),0);
}


void closeConnection(int socDes){   //sto disconnetendo il server
    inviaStringa(socDes,"CLS\0",LENTYPE);                
}

int notFirstMsg(const char* dest, const char* src, int* pflag){    //restituisce l'indice a cui si trova l'utente, nel buffer dei messaggi
    int i;
    for(i=0; i<=indexLastBuffer; i++){
        if(strcmp(bufferMsgs[i].user_dest,dest)==0 && strcmp(bufferMsgs[i].user_src,src)==0){
            *pflag=1;       //non era il primo messaggio
            return i;
        }
    }
    *pflag=0;       //era il primo messaggio
    indexLastBuffer++;
    return indexLastBuffer;       //ultimo utente    
}


void responseToHanging(int fd,const char * buffer){
    int i;
    uint32_t length = LENTYPE;
    uint32_t msg_number, timestamp;
    const char ack[4]="RTH\0";
    const char nack[4]="END\0";
    for(i=0; i<=indexLastBuffer; i++){  //scorro l'array di bufferMsg
        if(strcmp(buffer, bufferMsgs[i].user_dest)==0){ //ho trovato un insieme di messaggi per l'utente
           // MANDO messaggio RTH\0
           inviaStringa(fd,ack,length);
           //mando la risposta all'hang: utente src, numero msg, timestamp
           // utente sorgente
           length= strlen(bufferMsgs[i].user_src)+1;
           inviaStringa(fd,bufferMsgs[i].user_src,length);

           // numero messaggi
           length = sizeof(uint32_t);
            msg_number=bufferMsgs[i].lastIndexMsg+1;
           send(fd,(void*)&msg_number, sizeof(uint32_t),0);

           // timestamp ultimo
            timestamp=bufferMsgs[i].latest_time;
           send(fd,(void*)&timestamp, sizeof(uint32_t),0);
           
        }
    }
    /* MANDO messaggio END\0, ovvero ho trovato tutti gli utenti (anche nessuno) che hanno mandato
     messaggi all'utente mentre era offline*/
    inviaStringa(fd,nack,LENTYPE);
    
}

void stampa_menu(){
    printf("Digita il numero corrispondente al comando:\n");
    printf("1) help --> mostra i dettagli dei comandi\n");
    printf("2) list --> mostra un elenco degli utenti connessi\n");
    printf("3) esc  --> chiude il server\n");
    printf("****************************************************\n");
}

void help(){
    printf("*************************HELP***********************\n");
    printf("List --> stampa a video tutti gli utenti online, consultando la struttura dati presente in memoria\n");
    printf("Esc --> Disconnette  il server chiudendo prima tutte le connessioni con gli utenti online. Alcune operazioni non sono più disponibili per gli utenti. L'istante di uscita degli utenti che si disconnetteranno mentre il server è offline sarà memorizzato al prossimo login dello stesso utente.\n");
    printf("****************************************************\n");
    stampa_menu();
}

void list(){
    int i;
    printf("********************UTENTI ONLINE*******************\n");
    for(i=0; i<=indexLastOnlineDevices; i++){
        printf("%s\n",online[i].user_dest);
    }
    printf("****************************************************\n");
    stampa_menu();
}

int ricevi(int fd, void * mem){
    uint32_t length;
    int ret;
    ret = recv(fd,(void*)&length,sizeof(uint32_t),0);
    if(ret==0)
        return ret;
    if(ret<0){ printf("Errore in fase di ricezione\n");
        return ret;
    }                     
    ret=recv(fd,(void*)mem,length,0);
    if(ret==0){return ret;
    }
    if(ret<0){printf("Errore in fase di ricezione\n");
        return ret;
    } 
    return ret;        
}

void insert(struct onlineDevices s){    //inserisco un nuovo utente tra quelli online
    indexLastOnlineDevices++;
    online[indexLastOnlineDevices]=s;
}

int setLogOut(int socket,time_t time){
    FILE* fdl;    
    struct onlineDevices tmp;
    int i;
    struct tm timeOut=*localtime(&time);    //istante logout, passato come parametro
    struct tm timeIn;
    for(i=0; i<=indexLastOnlineDevices; i++){   //cerco l'utente tra quelli online
        if(online[i].socket==socket){
            //trovato
            tmp = online[i];
            online[i]=online[indexLastOnlineDevices--]; //compatto l'array, uno user online in meno
            timeIn = *localtime(&tmp.time);
            fdl = fopen("log_file","a");
            if(fprintf(fdl,"Utente: %s Porta: %d LogIn: %02d-%02d-%d %02d:%02d:%02d LogOut: %02d-%02d-%d %02d:%02d:%02d\n",tmp.user_dest, tmp.port, timeIn.tm_mday,timeIn.tm_mon + 1,timeIn.tm_year + 1900,timeIn.tm_hour, timeIn.tm_min, timeIn.tm_sec,timeOut.tm_mday,timeOut.tm_mon + 1,timeOut.tm_year + 1900,timeOut.tm_hour, timeOut.tm_min, timeOut.tm_sec )==0){ //controllo se c'è stato un errore nella scrittura
                fclose(fdl);
                return 0;   //non sono riuscito a scrivere nel file il record di Log
            }
            fclose(fdl);
            return 1;
        }
    }
    //se arrivo qui l'utente non era online
    return 0;
}

void closeAllConnection(){
    int i,tmp;
    tmp= indexLastOnlineDevices;
    for(i=0;i<=tmp; i++){
        close(online[0].socket);
        setLogOut(online[0].socket,0);     //metto istante di disconnessione fittizzio poichè utente ancora online
        //sempre online[0] perchè in setLogout ogni volta ricompatta l'array
    }
    printf("*****************SERVER DISCONNESSO*****************\n");
}

void salvaDisconnessioneOffline(int socket,time_t time){    //mi arriva istante di disconnessione relativa a disconnesion mentre serevr offline
    FILE* fdl;    
        int i;
    struct tm tm=*localtime(&time);
    for(i=0; i<=indexLastOnlineDevices; i++){
        if(online[i].socket==socket){
            //trovato
            fdl = fopen("log_file","a");
            fprintf(fdl,"Utente: %s LogOut della sessione precedente:  %02d-%02d-%d %02d:%02d:%02d\n",online[i].user_dest, tm.tm_mday, tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min, tm.tm_sec);
            fclose(fdl);
            return;
        }
    }
}

void changeUserStatus(const char* user, int port, int socket, time_t timestamp){
    //rende utente online
    struct onlineDevices s;     //serve per salvare tutte le informazioni, username, porta e socket
    strcpy(s.user_dest,user);
    s.port=port;
    s.time=timestamp;
    s.socket=socket;
    insert(s);  //inserisce nella struttura dati utenti online
}

int signup(const char* user, const char* psw, int port, int socket){
    FILE* fds;
    fds = fopen("signed_user","a");
    if(fprintf(fds,"%s %s\n",user,psw)==0){ //controllo se c'è stato un errore nella scrittura
        fclose(fds);
        return 0;
    }
    //ora rendo l'utente online
    changeUserStatus(user,port,socket,time(NULL)); //si veda implementazione funzione
    fclose(fds);   
    return 1;
}

int is_signed(const char* user){
    //serve a controllare se un utente è registrato
    // usata sia in fase di signup che in fase di login
    FILE* fds;
    char read[2049];    //user e psw possono essere al massimo da 1024 'una + lo spazio
    char *comparingString;
    fds = fopen("signed_user","r");
    while(fgets(read,1024,fds)){
        comparingString=strtok(read, " ");  //mi interessa solo lo username per controllare se è registrato
        if(strcmp(comparingString,user)==0){
            fclose(fds);   
            return 1;
        }       
    }
    fclose(fds);
    return 0;
}

int login(const char* user,const char* psw){
    FILE* fds;
    char read[2049];    //user e psw possono essere al massimo da 1024 'una + lo spazio
    char comparingString[2049];
    strcpy(comparingString,user);
    strcat(comparingString," ");
    strcat(comparingString,psw);        //devo controllare username+" "+psw
    fds = fopen("signed_user","r");
    while(fgets(read,1024,fds)){
        read[strlen(read)-1]='\0';
        if(strncmp(comparingString,read,strlen(read))==0){
            fclose(fds);   
            return 1;
        }       
    }
    fclose(fds);
    return 0; 
}

int main(int argc, char* argv[]){
	int listener, ret, new_sd, porta, command, fdmax, std_in, i;
	socklen_t len;
    struct sockaddr_in my_addr, client_addr;
    fd_set master, read_fds;
    uint32_t length;        //per la lunghezza dei messaggi
    uint16_t port;
    char buffer[1024];
    char user[1024];
    char psw[1024];

    if(argc > 1){       //se è stato avviato con ./serv porta
        porta=atoi(argv[1]);    //atoi converte stringa in int
    }else{
        porta=4242; //altrimenti di default prota 4242
    }
// ---------------- INIZIALIZZAZIONE DEL SERVER ----------------
    //pulizia dei set
	FD_ZERO(&master);	
	FD_ZERO(&read_fds);
    //creazione del socket di ascolto
	listener=socket(AF_INET,SOCK_STREAM,0);
	
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family=AF_INET;
	my_addr.sin_port = htons(porta);
	inet_pton(AF_INET, "127.0.0.1",&my_addr.sin_addr);

	ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret==-1){
		perror("Errore nella fase di inizializzazione del server: ");
		exit(1);
	}
    ret = listen(listener,SOMAXCONN);
    if(ret==-1){
       perror("Errore nella fase di inizializzazione del server: ");
		exit(1); 
    }
    FD_SET(listener, &master);
    fdmax=listener;
    std_in=fileno(stdin);
    FD_SET(std_in,&master);  //aggiungo lo std input al master set
    if(std_in > fdmax){ 
        fdmax=std_in;            //aggiorno il max
    }
    printf("*******************SERVER STARTED*******************\n");   
	stampa_menu();
// ---------------- INIZIO DELLA SELECT ----------------  
    while(1){
		read_fds = master;
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		for(i=0;i<=fdmax;i++){
			if(FD_ISSET(i, &read_fds)){
				//----------------PRONTO IL LISTENER----------------
                if(i==listener){
                    len=sizeof(client_addr);
					new_sd=accept(listener,(struct sockaddr*)&client_addr,&len);
                    FD_SET(new_sd,&master);
					if(new_sd>fdmax)
						fdmax=new_sd;
                //-----------------Fine listener--------------------
				}else if(i==std_in){
                    //-------------COMANDO DA TASTIERA-------------
					scanf("%d",&command);
                    switch(command){
                        case 1:
                            help();
                            break;
                        case 2:
                            list();
                            break;
                        case 3:
                            closeAllConnection();
                            exit(0);
                        default: break;
                    }
				}else{
                //----------------SOCKET DATI PRONTO ---------------------
                    if(ricevi(i,(void*)buffer)<=0){     //disconnessione di qualche client
                        FD_CLR(i,&master);
                        if(setLogOut(i,time(NULL))==0)
                            printf("Problemi nel memorizzare istante logout utente\n");      
                        close(i);
                    }else{
                        // printf("%s\n",buffer); //togliere il commento per vedere cosa riceve il server
                        if(strcmp(buffer,"SIG\0")==0 || strcmp(buffer,"LOG\0")==0){
                            ret = recv(i,(void*)&port,sizeof(uint16_t),0);  //ricevo la porta
                            if(ret<=0){
                                FD_CLR(i,&master);
                                if(setLogOut(i,time(NULL))==0)
                                    printf("Problemi nel memorizzare istante logout utente\n");
                                close(i);   //connessione chiusa;
                                return ret;
                            }
                            //riceve username
                            if(ricevi(i,(void*)user)<=0){
                                close(i);
                                FD_CLR(i,&master);
                                break; 
                            }
                            //riceve password
                            if(ricevi(i,(void*)psw)<=0){
                                close(i);
                                FD_CLR(i,&master);
                                break; 
                            }
                            
                            length = LENTYPE;
                            if(strcmp(buffer,"SIG\0")==0){      //operazione di signup
                                if(is_signed(user)==1){ // utente già registrato
                                    strcpy(buffer,"SUE\0"); //Sign-Up Error
                                }else if(signup(user,psw,ntohs(port),i)){
                                    strcpy(buffer,"ACK\0");
                                }else{
                                    strcpy(buffer,"SUE\0"); //Sign-Up Error
                                }
                                
                            }else if(strcmp(buffer,"LOG\0")==0){    //login
                                //login
                                if(is_signed(user)==1){ //l'utente è già registrato?
                                    if(login(user,psw)==1){ //Se si, la psw è giusta?
                                        strcpy(buffer,"ACK\0");
                                        //rendo utente online
                                        changeUserStatus(user,ntohs(port),i,time(NULL)); 
                                    }else{
                                        strcpy(buffer,"LIE\0"); //LogIn Error
                                    }
                                }else{
                                    strcpy(buffer,"LIE\0"); //LogIn Error
                                }
                            }
                            send(i,(void*)buffer,length,0);
                            fflush(stdout);
                            
                        }else if(strcmp(buffer,"CLS\0")==0){    //chiusura di una connessione
                            FD_CLR(i,&master);
                            if(setLogOut(i,time(NULL))==0)
                                printf("Problemi nel memorizzare istante logout utente\n");
                            close(i);   //connessione chiusa;
                        }else if(strcmp(buffer,"DLV\0")==0){    //chiesto se sono stati consegnati eventuali vecchi messaggi
                            ricevi(i,(void*)buffer);
                            notificaMessaggi(i,buffer);
                        }else if(strcmp(buffer,"HNG\0")==0){ 
                            //richiesta di hanging
                            ricevi(i,(void*)buffer);        //ricevo lo username
                            responseToHanging(i,buffer);
                        }else if(strcmp(buffer,"SHW\0")==0){ 
                            //richiesta di show messaggi pendenti
                            char tmp[1024];
                            ricevi(i,(void*)buffer);        //ricevo lo username
                            ricevi(i,(void*)tmp);       //ricevo username mittente da controllare
                            forwardMessages(i,buffer,tmp);
                        }else if(strcmp(buffer,"OUT\0")==0){   //un utente dopo il login mi ha segnalato che aveva fatto una disconnessione mentre ero offline
                            time_t t_disc;
                            recv(i,(void*)&t_disc,sizeof(time_t),0);
                            salvaDisconnessioneOffline(i,t_disc);
                        }else if(strcmp(buffer,"MSG\0")==0){
                            //chat
                            int indexBufferMsgs=-1;
                            int flag=0;
                            int dest_sd,dest_port;
                            char tmpC[1024];

                            ricevi(i,(void*)buffer);  //sorgente
                            strcpy(tmpC,buffer);
                            ricevi(i,(void*)buffer);  //destinatario
                            indexBufferMsgs = notFirstMsg(buffer,tmpC,&flag);   //è il primo messaggio da mittente a destinatario?
                            
                            if(flag==0){    //sì, è il primo messaggio, devo copiare mittente e destinatario
                                strcpy(bufferMsgs[indexBufferMsgs].user_src,tmpC);
                                strcpy(bufferMsgs[indexBufferMsgs].user_dest,buffer);
                                bufferMsgs[indexBufferMsgs].lastIndexMsg=-1;
                            }
                            ricevi(i,(void*)buffer);  //messaggio vero e proprio
                            bufferMsgs[indexBufferMsgs].lastIndexMsg++; //copio il messaggio nel buffer nella prossima istruzione
                            strcpy(bufferMsgs[indexBufferMsgs].msg[bufferMsgs[indexBufferMsgs].lastIndexMsg],buffer);
                            bufferMsgs[indexBufferMsgs].latest_time=(int)time(NULL);    //salvo istante ricezione
                            bufferMsgs[indexBufferMsgs].time[bufferMsgs[indexBufferMsgs].lastIndexMsg]=bufferMsgs[indexBufferMsgs].latest_time; //salvo orario nuovo messaggio
                            //se risulta online provo a connettermi e a mandare un messaggio
                            if(is_online(bufferMsgs[indexBufferMsgs].user_dest,&dest_port,&dest_sd,strlen(bufferMsgs[indexBufferMsgs].user_dest))){
                                //suppongo che se l'utente si trova nella struttura sia sicuramente online
                                uint16_t porta_da_spedire;  //chi ha avviato la chat deve sapere su quale porta contattare
	                            porta_da_spedire=htons(dest_port);  
                                //comunico al sorgente che l'utente dest è online
                                inviaStringa(i,"ONL\0",LENTYPE);
                                //mando la porta
                                send(i,(void*)&porta_da_spedire,sizeof(uint16_t),0);
                                
                                //inoltro il messaggio
                                inviaStringa(dest_sd,"MSG\0",LENTYPE);
                                //mando sorgente
                                length = strlen(bufferMsgs[indexBufferMsgs].user_src)+1;
                                inviaStringa(dest_sd,bufferMsgs[indexBufferMsgs].user_src,length);
                                //mando dest 
                                length = strlen(bufferMsgs[indexBufferMsgs].user_dest)+1;
                                inviaStringa(dest_sd,bufferMsgs[indexBufferMsgs].user_dest,length);
                                // mando messaggio vero e proprio
                                length = strlen(bufferMsgs[indexBufferMsgs].msg[bufferMsgs[indexBufferMsgs].lastIndexMsg])+1;
                                inviaStringa(dest_sd,bufferMsgs[indexBufferMsgs].msg[bufferMsgs[indexBufferMsgs].lastIndexMsg],length);
                                
                                //devo togliere il messaggio salvato dal buffer
                                bufferMsgs[indexBufferMsgs].lastIndexMsg--;
                            }else{
                            //comunico al client che dest è offline
                                inviaStringa(i,"NON\0",LENTYPE);
                            }
                            
                        }else if(strcmp(buffer,"SOU\0")==0){    //Show Online User, viene chiestose un certo utente è online
                            int porta;
                            char tmp[1024];
                            ricevi(i,tmp);
                            length=LENTYPE;
                             if(is_online(tmp, &porta, &ret,strlen(tmp))){ //controllo se online
                                inviaStringa(i,"UON\0",LENTYPE);
                                //ret, messa una variabile già dichiarata perchè in quetso frangente non ci interessa quel parametro
                            }else{
                                inviaStringa(i,"NON\0",LENTYPE);  //user Not ONlin
                                
                            }
                        }else if(strcmp(buffer,"RFP\0")==0){    //chiesta la porta di un utente
                            int porta;
                            uint16_t netPort;
                            memset((void*) buffer,0,sizeof(buffer));
                            ricevi(i,buffer);
                            if(is_online(buffer,&porta,&ret,strlen(buffer))==0)
                                //ret, messa una variabile già dichiarata perchè in quetso frangente non ci interessa quel parametro
                                //utente non online
                                porta=0;
                                
                            netPort = htons(porta);
                            length = sizeof(uint16_t);
                            send(i,(void*)&length,sizeof(uint32_t),0);
                            send(i,(void*)&netPort,length,0);
                               
                        }
                    }
                }
            }
	    }
    }
}
	

