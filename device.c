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
#define DIMBUFF 5 //Dimensione del buffer dei messaggi pendenti

struct socketUtente{    //ad ogni utente associo il socket sul quale avviene la comunicazione
    char user[1024];
    int sd;
};


struct bufferMessaggi{      //è la struttura che memorizza i messaggi che arrivano da altri utenti, prima che l'utente faccia show o chat
    char da[DIMBUFF][1024];
    char messaggio[DIMBUFF][10][1024];
    time_t orario[DIMBUFF][10];
    int ultimoMessaggio[DIMBUFF];
} bufferMsg;
int utentiPendenti=0;       //mi dice di quanti utenti ho bufferizzato i messaggi
int postoLibero=0;          //mi dice il primo postoLibero per memorizzare nuovi messaggi

void salvaMessaggio(const char* utente, const char* msg){
    // permette di bufferizzare un messaggio ricevuto se l'utente non ha fatto show o chat ancora
    int indice,i;
    if(utentiPendenti!=0){          //ho altri messaggi già salvati
        for(i=0; i<DIMBUFF;i++){
            if(strcmp(utente,bufferMsg.da[i])==0){
                //ho già altri messaggi da questo utente
                bufferMsg.ultimoMessaggio[i]++;     //incremento contatore ultimo messaggio da utente
                indice=bufferMsg.ultimoMessaggio[i];
                strcpy(bufferMsg.messaggio[i][indice],msg);     //copio il messaggio
                bufferMsg.orario[i][indice]=time(NULL);
                return;
            }
        }
    }
    //se sono arrivato qui non ho altri messaggi bufferizzati per l'utente
    if(postoLibero!=-1){        //se ci sono ancora posti liberi
        indice=bufferMsg.ultimoMessaggio[postoLibero]=0;
        strcpy(bufferMsg.da[postoLibero],utente);       //copio nome utente mittente
        strcpy(bufferMsg.messaggio[postoLibero][indice],msg);   //copio messaggio
        bufferMsg.orario[postoLibero][indice]=time(NULL);
        utentiPendenti++;       //incremento il numero di utenti pendenti
        if(utentiPendenti!=DIMBUFF){
            for(i=0; i<DIMBUFF;i++){
                if(strlen(bufferMsg.da[i])==0)      //ho trovato un posto libero
                    postoLibero=i;    //salvo il nuovo posto libero
            }
        }else
            postoLibero=-1;
    }else{
        printf("Buffer pieno, impossibile memorizzare messaggi perchè ce ne sono già in attesa dall'utente %s",utente);
    }
}
void scriviSuFile(const char* nome_file, const char* utente, const char* msg, time_t t){
    //memorizzo chat su file, memorizzata già nel formato da visualizzare così che basti leggere dal file per mostrare lo storico
    FILE *file=fopen(nome_file,"a");
    struct tm tm=*localtime(&t);
    
    if(file!=NULL){
        fprintf(file, "---------------------------\n");
        fprintf(file,"Da: %s\n",utente);
        fprintf(file, "Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
        fprintf(file, "%s\n",msg);
        fprintf(file, "---------------------------\n\n");
        fclose(file);
    }else{
        printf("Problema apertura file\n");
    }
}

void memorizzaChat(const char* nome_file, const char* utente){
    //salvo eventuali messaggi Bufferizzati
    int i,j;
    for(i=0; i<DIMBUFF;i++){
        if(strcmp(utente,bufferMsg.da[i])==0){
            //ho messaggi da questo utente
            for(j=0;j<=bufferMsg.ultimoMessaggio[i];j++){   //memorizzo tutti i messaggi
                scriviSuFile(nome_file,utente,bufferMsg.messaggio[i][j],bufferMsg.orario[i][j]);   //memorizzo il messaggio
            }
            strcpy(bufferMsg.da[i],"");     //"cancello" l'utente da quelli con messaggi "pendenti" (in attesa di essere scritti su file)           
            utentiPendenti--;
            postoLibero=i;      //il suo posto ora è libero
        }
    }
}


void inviaStringa(int socket,const char* stringa, uint32_t size){
    send(socket,(void*)&size,sizeof(uint32_t),0);
    send(socket,(void*)stringa,size,0);
}

void inviaMessaggio(int socket, const char* type, const char* src, const char* dest, const char* text, const char* nomeGruppo){
    //tipo potrebbere essere un MSG o un GMS (Group Message)
    inviaStringa(socket,type,LENTYPE);  //invio tipo
    if(strcmp(type,"GMS\0")==0){
        //se è una chat di gruppo devo indicare anche il nome
        inviaStringa(socket,nomeGruppo,strlen(nomeGruppo)+1);
    }
    inviaStringa(socket,src,strlen(src)+1); //invio sorgente
    inviaStringa(socket,dest,strlen(dest)+1);//invio destinatario  
    inviaStringa(socket,text,strlen(text)+1);// mando messaggio vero e proprio
}

int ricevi(int fd, void * mem){
    uint32_t length;
    int ret;
    ret = recv(fd,(void*)&length,sizeof(uint32_t),0);
    if(ret==0)
        return ret;
    if(ret<0){
        printf("Errore in fase di ricezione\n");
        return ret;
    }                     
    ret=recv(fd,(void*)mem,length,0);
    if(ret==0){
        return ret;
    }
    if(ret<0){
        printf("Errore in fase di ricezione\n");
        return ret;
    } 
    return ret;        
}

void riceviFile(int fd, uint32_t dim, char* user, char* from, int group, char * groupName){
    uint32_t ricevutiTotale=0;  
    uint32_t ricevuti=0;
    char buffer[1024];
    char nome_file[1024];       //sarà del tipo FileGiorno_Ora
    char nome_fileCompleto[1024]="FileScaricati/";      //i file vengono memorizzati in una directory, perciò ci va aggiunto in testa il nome della directory
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    //imposto il nome del file con la sprintf
    sprintf(nome_file,"File%d-%02d-%02d_%02d:%02d",tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    strcat(nome_fileCompleto,nome_file);
    
    FILE *file=fopen(nome_fileCompleto,"a");        //sostanzialmente ricevo il file e lo copio in uno nuovo, che copio volta per volta, ogni volta che ricevo un "chunk"
    memset((void*)buffer,0,1024);
    while(ricevutiTotale<dim){      //non è detto che arrivi tutto il chunk in un'unica volta, e soprattutto potrebbero arrivare più chunk
        ricevuti = recv(fd,(void*)buffer,1024,0);
        ricevutiTotale+=ricevuti;
        if(ricevuti==-1 || strlen(buffer)==0 || file==NULL){
            //errore
            if(file!=NULL){     //errore non è nell'apertura del file
                fclose(file);
            }
            remove(nome_fileCompleto);
            return;
        }else{
           fwrite(buffer, sizeof(char),strlen(buffer),file);
        }
    }
    fclose(file);
    printf("------FILE-----\n");
    printf("L'utente %s ha mandato un file ",from);
    if(group==1)
        printf("nel gruppo %s",groupName);
    printf("\n");
    printf("Ricevuto il file: %s\n",nome_file);
    printf("File memorizzato nella directory FileScaricati\n");                    
                    
}

int chiediPorta(int sd,const char* dst){
    //metodo utilizzato quando si vuole stabilire una connessione con un altro utente, perciò è necessario chiedere la porta sul quale ascolta al server
    uint16_t porta;
    int ret;
    inviaStringa(sd,"RFP\0",LENTYPE);   //Request For Port
    inviaStringa(sd,dst,strlen(dst)+1); //nome dell'utente di cui si chiede la porta
    if(ricevi(sd,(void*)&porta)<=0)
        return 0;
    ret = ntohs(porta);
    return ret;
}

int stabilisceConnessionePeer(struct sockaddr_in* other_peer, const char* user,int sd, uint16_t peer_port){
    //ultimo parametro se conosco già la porta del peer e non devo passare dal server
    int flag=0; //usata dopo nella connect
    int socketPeer;
    int ret;
    memset((void*)other_peer,0,sizeof(*other_peer));
    other_peer->sin_family=AF_INET;
    if(peer_port==0){
        //devo ricevere porta
        ret=recv(sd,(void*)&peer_port,sizeof(uint16_t),0);
        //ricevo la porta, non devoinvocare richiedi porta in quanto questa richiesta avviene solo in risposta al primo 
        // messaggio inviato e il server sa che deve inviare il numero di  porta del destinatario (formato del messaggio)
        if(ret<=0){
            return 0;
        }
    }
    other_peer->sin_port=peer_port;
    inet_pton(AF_INET,"localhost",&other_peer->sin_addr);	
    //provo a connettermi all'altro peer, per 3 volte 
    socketPeer=socket(AF_INET,SOCK_STREAM,0);
    while(connect(socketPeer,(struct sockaddr*)other_peer,sizeof(*other_peer))<0 && flag<3){
        flag++;
    }
    if(flag>=3){
        return 0;
    }
    //comunico all'altro host che ha iniziato una connessione con me
    //altrimenti se l'altro utente aprisse la chat con me, aprirebbe un nuovo socket
    inviaStringa(socketPeer,"SCN\0",LENTYPE);
    inviaStringa(socketPeer,(void*)user,strlen(user)+1);
                                    
    return socketPeer;
}


void menu_avvio(){
    printf("Digita il numero dell'operazione\n");
    printf("1 --> Signup\n2-->Login\n");
    printf("**********************************************\n");
}

void menuPrincipale(){
    printf("\n***************MENU PRINCIPALE****************\n");
    printf("hanging\n");
    printf("show 'username'\n");
    printf("chat 'username'/'nome gruppo'\n");
    printf("out \n");
}

int getCommand(char buf[]){
   //metodo che associa un comando ad un numero, così da poter fare lo switch su una var intera
    if(strcmp(buf,"out")==0){
        return 7;
    }
    else if(strcmp(buf,"hanging")==0){
        return 4;
    }
    else if(strncmp(buf,"show ",5)==0){
        return 5;
    }else if(strncmp(buf,"chat ",5   )==0){
    
        return 6;
    }else{        
        return -1; //nessun comando valido
    }
}

int chiediOnline(int sd, const char* chi){
    //chiedo al server se l'utente "chi" è online
    //chiedo al server perchè potrei non avere comunicato con lui
    char tipo[LENTYPE]="SOU\0";
    uint32_t length = LENTYPE;
    inviaStringa(sd,tipo,length);

    length=strlen(chi)+1;
    inviaStringa(sd,chi,length);
    ricevi(sd,(void*)tipo);

    if(strcmp(tipo,"UON\0")==0){        //se User Online --> restituisco 1
        return 1;
    }else       //altrimenti è offline
        return 0;
}

int controllo_rubrica(const char* src, const char *dst){
    if(strcmp(src,"user1")==0 && strcmp(dst,"user2")==0){
        return 1;
    }

    if(strcmp(src,"user2")==0){
        if(strcmp(dst,"user1")==0 || strcmp(dst,"user3")==0)
            return 1;
        return 0;
    }
    if(strcmp(src,"user3")==0 && strcmp(dst,"user2")==0)
        return 1;
    else return 0;
}

int cercaSocketUtente(const char* user, const struct socketUtente* socketUtente, int index){
    //cerco nella struttura socketUtente se è presente una corrispondenza tra utente e socket
    //Se si, significa che ho già comunicato con lui
    int i;
    for(i=0; i<=index;i++){
        if(strcmp(user,socketUtente[i].user)==0)
            return socketUtente[i].sd;
    }
    return 0;
}

void creaGruppo(char utenti[][1024], int lastIndex, const struct socketUtente* sockUtente, int indexSocket, const char* user){
    int j,sd,k;
    uint32_t nUtenti = lastIndex+2; //+2 perchè: uno è l'utente che sta creando il gruppo e l'altro è perchè l'indice è sempre -1
    FILE *chatGruppo;
    char nomeChat[1024];    //chat_nomeGruppo
    for(j=0;j<=lastIndex;j++){
        sd=cercaSocketUtente(utenti[j],sockUtente,indexSocket);
        inviaStringa(sd,"GRP\0",LENTYPE);
        //comunico il nome del gruppo, in questo caso siccome ce ne potrà essere al massimo uno, il nome è standard
        //serve per poter aprire la chat quando l'utente riceve i messaggi o vuole uscire e poi riaprire
        inviaStringa(sd,"gruppo1",strlen("gruppo1")+1);
        //comunico il numero di utenti e mando gli username
        send(sd,(void*)&nUtenti,sizeof(uint32_t),0);
        //comunico tutti gli utenti
        for(k=0;k<=lastIndex;k++){
            inviaStringa(sd,utenti[k],strlen(utenti[k])+1);
        }
        inviaStringa(sd,user,strlen(user)+1);       //comunico il mio username
        
    }
    //sto creando il gruppo, elimino eventuali messaggi da un eventuale file di un gruppo che in precedenza si chiamava come questo
    strcpy(nomeChat,"chat_");
    strcat(nomeChat,"gruppo1");
    chatGruppo= fopen(nomeChat,"w");
    fprintf(chatGruppo,"******************CHAT DI GRUPPO********************\n");
    fclose(chatGruppo);
}

int controllaStabilisciConnessione(char * userDest, char* userSrc, int sokcetServer,fd_set *master, int *fdmax, struct socketUtente* socketUtentiChat, int* lastIndexSocketUtenti,struct sockaddr_in* other_peer){
    int flag=0;
    int socket_other_peer,j;
    //controllo le connessioni già presenti senza dover contattaree il server
    
    if(chiediOnline(sokcetServer,userDest)==1){
        for(j=0;j<=(*lastIndexSocketUtenti);j++){
            if(strcmp(socketUtentiChat[j].user,userDest)==0){
                //ho già stabilito una connessione con l'utente in precedenza
                return 1;
            }
        }
        if(flag==0){
            uint16_t porta;
            int res;
            //devo stabilire connessione, ma prima devo chiedere la porta al server
            res=chiediPorta(sokcetServer,userDest);
            if(res==0){
                printf("Errore inaspettato\n");
                return 0;
            }
            porta=htons(res);
            socket_other_peer=stabilisceConnessionePeer(other_peer, userSrc,sokcetServer,porta);
            if(socket_other_peer==0){
                printf("Errore inaspettato, impossibile stabilire chat di gruppo\n");
                return 0;
            }
            (*lastIndexSocketUtenti)++;     //c'è una nuova associazione utente-socket --> incremento l'ultimo index
            strcpy(socketUtentiChat[*lastIndexSocketUtenti].user,userDest); //copio nome utente
            socketUtentiChat[*lastIndexSocketUtenti].sd=socket_other_peer;  //copio il socket descriptor
            FD_SET(socket_other_peer,master); //così da poter ricevere i messaggi
            if(socket_other_peer>*fdmax)
                *fdmax=socket_other_peer;
        
        }
    }else{
        printf("Utente offline, impossibile stabilire chat di gruppo\n");
        return 0;
    }
    return 1;
}

void inviaMessaggioGruppo(const char* user, const char* nomeGruppo, char gruppo[][1024],int lastDestUser, const char* msg,struct socketUtente* socketUtentiChat, int lastIndexSocketUtenti){
    int j,k;
    int socket;
    char nome_file[1024];
    char tmp[1024];
    time_t t;
    struct tm tm;
    strcpy(nome_file,"chat_");
    strcat(nome_file,nomeGruppo);

    for(j=0; j<=lastDestUser; j++){     //invio a tutti i partecipanti "GMS\0"
        for(k=0;k<=lastIndexSocketUtenti;k++){
            if(strcmp(gruppo[j],socketUtentiChat[k].user)==0){
                socket = socketUtentiChat[k].sd;
                inviaMessaggio(socket,"GMS\0",user,gruppo[j],msg, nomeGruppo);   //Group MeSsage
                break;
            }
        }
        
    }
    printf("\033[A\33[2K");//cancello il messaggio appena scritto da terminale
    fflush(stdout);
    printf("---------------------------\n");
    printf("Da: %s\n",user);
    t = time(NULL);
    tm = *localtime(&t);
    printf("Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
    sprintf(tmp,"**%s",msg);
    //sono sicuro abbia consegnato il messaggio, gli utenti del gruppo sono tutti online
    printf("%s\n",tmp);
    printf("---------------------------\n\n");
                                        
    //memorizzo sul file
    scriviSuFile(nome_file,user,msg,t); //salvo il messaggio sul file                               
}

void inviaNotificaGruppo(char gruppo[][1024],int lastDestUser, void* notifica,struct socketUtente* socketUtentiChat, int lastIndexSocketUtenti, int mode){
    //mode = 0, devo inviare una stringa come ad esempio 'GSH\0', mode 1 --> devo mandare un uint32_t
    int j,k, socket;
    for(j=0; j<=lastDestUser; j++){
        for(k=0;k<=lastIndexSocketUtenti;k++){
            if(strcmp(gruppo[j],socketUtentiChat[k].user)==0){
                socket = socketUtentiChat[k].sd;
                if(mode==0)
                    inviaStringa(socket,(char *)notifica,LENTYPE);
                else
                    send(socket,notifica,sizeof(uint32_t),0);
                break;
            }
        }
    }
    
}

void showMessage(int sd, const char* user, const char* userRemoto, int operazione){
    uint32_t n_buf;
    uint32_t j;
    char buffer[1024];
    char nome_file[1024];
    strcpy(nome_file,"chat_");
    strcat(nome_file,user);
    strcat(nome_file,userRemoto);

    //mando la richiesta SHW\0 (Show)
    inviaStringa(sd,"SHW\0",LENTYPE);
    //mando il mio nome utente
    inviaStringa(sd,user,strlen(user)+1);
    //mando il nome dell'utente di cui voglio visualizzare i messaggi
    inviaStringa(sd,userRemoto,strlen(userRemoto)+1);
    
    //ricevo numero di messaggi
    recv(sd,(void*)&n_buf,sizeof(uint32_t),0);
    //ricevo i messaggi
    for(j=0; j<(n_buf); j++){
        time_t orario;
        ricevi(sd,(void*)buffer);
        recv(sd,(void*)&orario,sizeof(time_t),0);
        scriviSuFile(nome_file,userRemoto,buffer, orario);      //a prescindere dall'operazione salvo il messaggio su file, se sono quelli pendenti vanno sicuramente prima degli altri
        if(operazione==0){  //stessa funzione sia per comando show che quando bisogna aprire la chat
            struct tm tm=*localtime(&orario);
            //0 --> show, quindi stampo subito il messaggio;
            // 1--> chat, non mostro subito perchè prima leggo tutto il file della chat
            printf("---------------------------\n");
            printf("%s\n",buffer);
            printf("Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
            printf("---------------------------\n");
        }
    }

}

void lasciaGruppo(const char* user,char gruppo[][1024],int lastDestUser,struct socketUtente* socketUtentiChat, int lastIndexSocketUtenti){
    int j,k;
    int socket;
    for(j=0; j<=lastDestUser; j++){     //devo comunicarlo a tutti i partecipanti del gruppo
        for(k=0;k<=lastIndexSocketUtenti;k++){
            if(strcmp(gruppo[j],socketUtentiChat[k].user)){
                socket = socketUtentiChat[k].sd;
                break;
            }
        }
        inviaStringa(socket,"LEV\0",LENTYPE);       //Leave Group
        inviaStringa(socket,user,strlen(user)+1);
    }
}

int inviaChunkUtente(const char* dest, void* chunk,int dim, struct socketUtente* socketUtentiChat, int lastIndexSocketUtenti){
    int k, socket, nByteSpediti, nByteSpeditiDopo;
    nByteSpediti=nByteSpeditiDopo=0;
    for(k=0;k<=lastIndexSocketUtenti;k++){      //cerco il socket su cui comunico con l'utente
        if(strcmp(dest,socketUtentiChat[k].user)==0){
            socket = socketUtentiChat[k].sd;
            break;
        }
    }
    while(nByteSpediti<dim){        //la pate di file potrebbe non stare tutta nel buffer del sistema, quindi ciclo finchè non viene scritto tutto
        nByteSpeditiDopo = send(socket,(void*)(chunk+nByteSpediti),dim,0);
            
        if(nByteSpeditiDopo==-1){
            memset((void*)chunk,0,strlen(chunk));
            send(socket,(void*)chunk,dim,0);
            printf("Errore: impossibile mandare il file all'utente %s\n",dest);
            return -1;
        }
        nByteSpediti += nByteSpeditiDopo;
    }
    return 1;
}

void inviaChunkGruppo(char gruppo[][1024],int lastDestUser,void* chunk,int dim,struct socketUtente* socketUtentiChat, int lastIndexSocketUtenti){
    int j;
    for(j=0; j<=lastDestUser; j++){
        if(inviaChunkUtente(gruppo[j],chunk,dim,socketUtentiChat,lastIndexSocketUtenti)==-1){
            printf("Impossibile condividere file con utente %s",gruppo[j]);
        }
    }
}

void chiudiConnessioni(int socket,int serverSocket,struct socketUtente* socketUtentiChat, int *lastIndexSocketUtenti, char dest_user[][1024], int* lastDestUser,int is_group, const char* nomeGruppo,fd_set *master){
    int j=0;
    // è arrivato un messaggio di close o una disconnesione improvvisa
    if(socket==serverSocket){      //sd è il socket di comunicazione col server
        printf("!!!!!!!!!!!!!!!!!!!!!!!!! ATTENZIONE !!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("Server offline.\nè possibile che alcune operazioni non siano disponibili al momento\n");
        printf("--------------------------------------------------------------\n"); 
    }else{
        char utenteDisconnesso[1024];
        while(j<=(*lastIndexSocketUtenti)){     //devo eliminare tutti gli eventuali socket di comunicazione presenti con quel server
            //e devo ricompattare la struttura socketUtenti
            if(socket==socketUtentiChat[j].sd){
                strcpy(utenteDisconnesso,socketUtentiChat[j].user);
                strcpy(socketUtentiChat[j].user,socketUtentiChat[*lastIndexSocketUtenti].user);
                socketUtentiChat[j].sd = socketUtentiChat[*lastIndexSocketUtenti].sd;
                *lastIndexSocketUtenti= (*lastIndexSocketUtenti)-1;
            }else
                j++;
        }
        if(strlen(nomeGruppo)>0){
            //ho una chat con un gruppo attiva
            j=0;
            while(j<=(*lastDestUser)){
                //se l'utente faceva parte del gruppo, lo abbandona automaticamente, devo toglierlo dai partecipanti
                //devo anche ricompattare la struttura dati che mantiene il nome dei partecipanti
                if(strcmp(utenteDisconnesso,dest_user[j])==0){      
                    strcpy(dest_user[j],dest_user[*lastDestUser]);
                    *lastDestUser= (*lastDestUser)-1;
                    if(is_group==0)
                        printf("------Notifica-----\n");
                    printf("%s ha abbandonato il gruppo\n",utenteDisconnesso);  
            
                }else
                    j++;
            }
        }
    }
    close(socket);
    FD_CLR(socket,master);
}                                

int main(int argc, char* argv[]){
    socklen_t len;
	int my_port, server_port;
	int command;        //memorizza il comando preso da tastiera, se 1 signup, 2 login
	int listener,ret, i,j, fdmax, new_sd,sd, socket_other_peer, socket_com;
    int is_chatting=0;      //l'utente sta chattando (1) oppure no (0)
    int is_group=0;         //è una chat di gruppo?(1), se no (0)
    int serverOnline=1;     //indica se è possibile fare operazioni con il server (1), se no (0)
    struct socketUtente socketUtentiChat[10];    //istanzio la struttura per associare utente - socket attualmente usato
    int lastIndexSocketUtenti=-1;           // indica ultimo utente presente nell'array precedente
	struct sockaddr_in server_addr, my_addr, other_peer;
	char user[1024];
    char psw[1024];
    char buffer[1024];
    char nome_file[1024];
    char dest_user[10][1024];  //gli utenti del grupppo--> 5 utenti per gruppo
    int lastDestUser=-1;        //ultimo utente presente nell'array dei partecipanti al gruppo
    char nomeGruppo[1024];      // nome del gruppo (per questioni di tempo implementato un solo gruppo possibile per volta)
    char utenteChatSingola[1024];       //nome dell'utente con il quale si sta chattando attualmente
    char *token;                //necessario per i comandi come chat username o show username dove devo dividere la stringa dopo uno spazio
    uint32_t length = LENTYPE;
    uint32_t n_buf;         //numeric buffer per ricevere numeri dal socket
    fd_set master, read_fds;
    time_t t;
    struct tm tm;
    FILE *file;     //usato ad esempio per aprire il file che contiente l'istante dell'ultima disconnessione offline
    

    my_port=atoi(argv[1]);    //atoi converte stringa in int
                             // il primo parametro viene considerato con indice 1
	//creazione socket di ascolto 
	listener= socket(AF_INET, SOCK_STREAM,0);
    memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family=AF_INET;
	my_addr.sin_port = htons(my_port);
	inet_pton(AF_INET, "localhost",&my_addr.sin_addr);
    ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr));

	if(ret==-1){
		perror("Errore nella fase di inizializzazione: ");
		exit(1);
	}
    //creo il socket di comunicazione con il server, non c'è bisogno di fare bind in quanto questo può avere una porta qualsiasi
    sd=socket(AF_INET,SOCK_STREAM,0);
    printf("****************DEVICE AVVIATO****************\n");
    printf("Porta di ascolto: %d\n",my_port);
do{ 
    //scelta del comando inizialie: SIGNUP O LOGIN
    do{
        menu_avvio();
        scanf("%d",&command);
        if(command!=1 && command!=2)
            command=0;
    }while(command==0);
    printf("********************DIGITA********************\n");
    printf("NumeroPortaServer username password\n");
    scanf("%d %s %s", &server_port, user, psw);
    //connessione con server
	memset((void*)&server_addr,0,sizeof(server_addr));
	server_addr.sin_family=AF_INET;
	server_addr.sin_port=htons(server_port);
	inet_pton(AF_INET,"localhost",&server_addr.sin_addr);	
	if(strncmp("LIE\0",buffer,LENTYPE)!=0 && strncmp("SUE\0",buffer,LENTYPE)!=0){
        //LIE --> LogIn Error; SUE --> SignUp Error   
        //ha sbagliato user o psw, non deve riconnettersi
	    ret =connect(sd,(struct sockaddr*)&server_addr,sizeof(server_addr));
	    if(ret==-1){
		    perror("Errore, server non raggiungibile: ");
		    exit(1);
	    }
    }
    // fine della connessione e inizio scambio messaggi per login o signup
   
    if(command==1)
        inviaStringa(sd,"SIG\0",LENTYPE);
    else //command == 2
        inviaStringa(sd,"LOG\0",LENTYPE);
    
    //devo comunicare il mio numero di porta sul quale mi metterò in ascolto
    send(sd,(void*)&my_addr.sin_port,sizeof(uint16_t),0);
    //send username
    length = strlen(user)+1;
    inviaStringa(sd,user,length);
    //send psw
    length = strlen(psw)+1;
    inviaStringa(sd,psw,length);
    //ricevo esito dell'operazione
    length = LENTYPE;
    recv(sd,(void*)buffer,length,0);

    if(strcmp(buffer,"ACK\0")==0){
      //vedo se l'ultima volta che mi sono disconnesso il server era offline
      file = fopen(user,"r");
      if(file!=NULL){
        time_t istanteUscita;
        fscanf(file,"%lu",&istanteUscita);
        fclose(file);
        inviaStringa(sd,"OUT\0",LENTYPE);
        send(sd,(void*)&istanteUscita,sizeof(time_t),0);
      }
      remove(user);     //rimuovo l'eventuale file che si chiama come l'utente dove è memorizzato l'istante di uscita
      break;
    }
        
    //se esito negativo rinizia da capo
    printf("Username o password non corretti\n");
}while(1);

    printf("\n*******************WELCOME********************\n");
    menuPrincipale();
    //----------------FINE PARTE COMUNICAZIONE ESCLUSIVA CLIENT-SERVER----------------
    ret = listen(listener,SOMAXCONN);       //il vaklog è il massimo possibile
    if(ret==-1){
       perror("Errore dopo la connessione al server: ");
	    exit(1); 
    }
    //Gestione degli FD
    //pulizia dei set
	FD_ZERO(&master);	
	FD_ZERO(&read_fds);
    FD_SET(listener, &master);
    FD_SET(sd, &master);
    fdmax=sd > listener ? sd : listener;        //setto fdmax
    FD_SET(fileno(stdin),&master);  //aggiungo lo std input al master set
    if(fileno(stdin) > fdmax){ 
        fdmax=fileno(stdin);            //aggiorno il max
    } 
    fflush(stdout);
  
    //chiedo al server se ci sono stati dei messaggi per me
    inviaStringa(sd,"DLV\0",LENTYPE);       //DLV, come il tipo che devo ricevere
    inviaStringa(sd,user,strlen(user)+1);

    // ---------------- INIZIO DELLA SELECT ----------------
    while(1){
		read_fds = master;
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		for(i=0;i<=fdmax;i++){
            if(FD_ISSET(i, &read_fds)){
				//----------------PRONTO IL LISTENER----------------
                if(i==listener){
					len=sizeof(other_peer);
					new_sd=accept(listener,(struct sockaddr*)&other_peer,&len);
                    FD_SET(new_sd,&master);
					if(new_sd>fdmax)
						fdmax=new_sd;
                //-----------------Fine listener--------------------
				}else if(i==0){
                    //----------------PRONTO LO STDIN----------------
                    fgets(buffer,1024,stdin);
                    buffer[strlen(buffer)-1]='\0';    //fgets prende \n, lo sostituisco con \0
                    if(is_chatting){
                        //l'input non è un comando del menu, ma un comando della chat o un messaggio
                        if(strcmp(buffer,"\\u\0")==0){  //vuole vedere utenti online
                            printf("------------Utenti online------------\n");
                            if(serverOnline!=0){        //è una delle operazioni che coinvolge il server
                                // IN SOSTITUZIONE ALLA GESTIONE DELLA RUBRICA, come richiesto
                                if(strcmp(user,"user1")==0 
                                || strcmp(user,"user3")==0){
                                    if(chiediOnline(sd,"user2")==1)
                                        printf("user2\n");
                                }else if(strcmp(user,"user2")==0){
                                    if(chiediOnline(sd,"user1")==1)
                                        printf("user1\n");
                                    if(chiediOnline(sd,"user3")==1)
                                        printf("user3\n");
                                }
                            }else
                                printf("Impossibile eseguire operazione, server offline\n");
                            printf("-------------------------------------\n");
                        }else if(strncmp(buffer,"\\a ",2)==0){    //vuole aggiungere un utente
                            //dopo avere capito il comando, la parte interessante della string è quella dopo lo spazio
                            token=strtok(buffer, " ");
                            token = strtok(NULL, " ");  //ho estratto lo username dal comando
                            if(controllo_rubrica(user,token)==0){
                                printf("Utente inesistente, impossibile aggiungerlo alla chat\n");
                                break;
                            }
                            //devo controllare che l'utente non sia già nella chat
                            for(j =0;j<=lastDestUser;j++){
                                if(strcmp(token,dest_user[j])==0){
                                    printf("Utente già presente all'interno della chat, impossibile aggiungerlo\n");
                                    break;
                                }
                            }
                            if(serverOnline!=0){       // devo stabilire connessione con utenti con i quali non ho connession
                                if(controllaStabilisciConnessione(dest_user[0],user,sd,&master,&fdmax,socketUtentiChat,&lastIndexSocketUtenti,&other_peer) == 0){
                                    break;      //questo era l'utente con il quale sto già chattando
                                }
                                if(controllaStabilisciConnessione(token,user,sd,&master,&fdmax,socketUtentiChat,&lastIndexSocketUtenti,&other_peer) == 0){
                                    break;      //utente che voglio aggiungere, ma offline
                                }else{      //se online invece
                                    //devo aggiungere utente alla chat, dest_user[0] già presente quando ho avviato chat
                                    lastDestUser++;
                                    strcpy(dest_user[lastDestUser],token);
                                }
                            }else{
                                printf("Impossibile eseguire operazione, server offline\n");
                                break;
                            }
                            //posso comunicare a tutti la creazione del gruppo
                            creaGruppo(dest_user,lastDestUser,socketUtentiChat,lastIndexSocketUtenti,user);
                            is_group=1;     //setto la variabile, segnando così il fatto che l'utente è in una chat di gruppo
                            strcpy(nomeGruppo,"gruppo1\0");     //come detto, il gruppo è uno ed il nome è standard
                        }else if(strncmp(buffer,"\\q",2)==0){    //vuole uscire dalla chat
                            is_chatting=0;      //lo stdin viene interpretato come un comando per il menu principale
                            is_group=0;
                            menuPrincipale();
                            break;
                            
                        }else if(strncmp(buffer,"\\l",2)==0){    //vuole abbandonare il gruppo
                            if(is_group==1){
                                lasciaGruppo(user,dest_user,lastDestUser,socketUtentiChat,lastIndexSocketUtenti);
                                is_group=0;
                                is_chatting=0;
                                lastDestUser=-1;
                                strcpy(nomeGruppo,"");      
                            }   //se non è in una chat di gruppo funziona come una \q
                            menuPrincipale();
                            break;
                            
                        }else if(strncmp(buffer,"\\s",2)==0){    // condivisione di file
                            int flag=0;
                            uint32_t size;
                            //anche in questo caso, ci interessa prendere la parte presa in input dopo lo spazio
                            token=strtok(buffer, " ");
                            token = strtok(NULL, " ");
                            FILE *file=fopen(token,"r");
                            if(file==NULL){     //impossibile aprire il file (ad esempio: non esiste)
                                printf("Impossibile trovare il file\n");
                                break;
                            }else{
                                int s; //socket descriptor
                                printf("-------------Condivisione file in corso-------------\n");
                                
                                if(is_group==1){    //è una condivisione di un file con un gruppo
                                    char notifica[4] = "GSH\0"; //Group Share, lo notifico diversamente solo per il fatto che così l'utente destinatario distingue se è un file indirizzato solo a lui o meno
                                    inviaNotificaGruppo(dest_user,lastDestUser,(void*)notifica,socketUtentiChat,lastIndexSocketUtenti,0);
                                }else{
                                    char notifica[4] = "SHR\0";     //Share
                                    //condivisione in chat singola
                                    //devo recuperare per prima cosa il socket nel quale comunico con l'utente
                                    for(j=0;j<=lastIndexSocketUtenti;j++){
                                        if(strcmp(utenteChatSingola,socketUtentiChat[j].user)==0){
                                            //trovato socket di comunicazione con l'utente
                                            s = socketUtentiChat[j].sd;
                                            inviaStringa(s,(void*)notifica,LENTYPE);
                                            break;
                                        }
                                    }
                                    if(j>lastIndexSocketUtenti){
                                        uint16_t portaUtente; 
                                        //non ho una connessione con l'utente a cui voglio mandare il file, devvo prima crearla
                                        portaUtente = chiediPorta(sd,utenteChatSingola);    //prima devo conoscere la porta
                                        if(portaUtente==0){
                                            printf("Utente offline, impossibile condividere file\n");
                                            break;
                                        }
                                        s=stabilisceConnessionePeer(&other_peer, user,sd,htons(portaUtente));
                                        if(s==0)
                                            break;
                                        inviaStringa(s,(void*)notifica,LENTYPE);        //invio SHR\0
                                        lastIndexSocketUtenti++;
                                        strcpy(socketUtentiChat[lastIndexSocketUtenti].user,utenteChatSingola);
                                        socketUtentiChat[lastIndexSocketUtenti].sd=s;
                                        FD_SET(s,&master); //così da poter ricevere i messaggi
                                        if(s>fdmax)
						                    fdmax=s;
                                    }
                                }
                                //mando la dimensione del file
                                fseek(file, 0, SEEK_END);       //posiziona il cursore alla fine del file
                                size=ftell(file);
                                rewind(file);                   //riposiziono il cursore all'inizio del file
                                if(is_group==1){    //è una condivisione di un file con un gruppo
                                    inviaNotificaGruppo(dest_user,lastDestUser,(void*)&size,socketUtentiChat,lastIndexSocketUtenti,1);
                                }else{
                                    send(s,(void*)&size,sizeof(uint32_t),0);
                                }
                                for(j=0;j<(int)size;j+=1024){
                                memset(buffer,0,1024);
                                    flag=fread((void*)buffer, 1023, 1, file);   //leggo 1023 byte per volta per non copiare tutto il file in memoria, specialmente se molto grande
                                    buffer[1024]='\0';          //se il file è più grande rischio: segmentantion fault / di inviare dati presi da altre strutture dati in memoria
                                    if(flag<1 && feof(file)!=1){
                                        //fread non ha letto 1024 byte e non perchè è arrivata a fine file, è un errore
                                        //mando un chunk vuoto
                                        memset((void*)buffer,0,strlen(buffer));
                                        printf("Errore, impossibile condividere il file\n");
                                    }

                                    if(is_group==1)
                                        inviaChunkGruppo(dest_user,lastDestUser,(void*)buffer,1024,socketUtentiChat,lastIndexSocketUtenti);
                                        //non c'è bisogno di fare il controllo degli utenti online, quando un utente si disconnette esce dal gruppo automaticamente
                                    else{
                                        if(chiediOnline(sd,utenteChatSingola)==1)   //l'utente si potrebbe essere disconnesso
                                            inviaChunkUtente(utenteChatSingola,(void*)buffer,1024,socketUtentiChat,lastIndexSocketUtenti);
                                        else
                                            printf("Impossibile condividere file, utente offline al momento\n");
                                    }
                                }
                                printf(".\n.\n.\n");
                                printf("-------------Condivisione file terminata------------\n");
                                
                            }
                        }else{  //è un messaggio      
                            char msg[1024];
                            if(is_group==1){        //sono in una chat di gruppo
                                inviaMessaggioGruppo(user,nomeGruppo, dest_user,lastDestUser,buffer,socketUtentiChat,lastIndexSocketUtenti);
                            }else{
                                socket_com=sd;  //nel caso in cui debba parlare con il server                   
                                //cerco se è già presente una connessione con l'utente
                                for(j =0;j<=lastIndexSocketUtenti;j++){
                                    if(strcmp(utenteChatSingola,socketUtentiChat[j].user)==0){
                                        socket_com = socketUtentiChat[j].sd;
                                        break;
                                    }
                                }
                                if(socket_com==sd && serverOnline==0){
                                    printf("Impossibile eseguire operazione, server offline\n");
                                    break;
                                }
                                //i messaggi scambiati sono gli stessi, cambia solo il dest    
                                inviaMessaggio(socket_com,"MSG\0",user,utenteChatSingola,buffer,"");
                                strcpy(nome_file,"chat_");
                                strcat(nome_file,user);
                                strcat(nome_file,utenteChatSingola);
                                //se sto chattando, sono sicuro di poter scrivere perchè ho già salvato i messaggi pendenti
                                printf("\033[A\33[2K");//cancello il messaggio appena scritto da terminale
                                fflush(stdout);
                                strcpy(msg,buffer);
                                printf("---------------------------\n");
                                printf("Da: %s\n",user);
                                t = time(NULL);
                                tm = *localtime(&t);
                                printf("Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
          
                                if(socket_com==sd){     //ho mandato il messaggio al server perchè utente dest offline oppure perchè primo messaggio
                                    //attendo risposta dal server, se l'utente è online o meno
                                    if(ricevi(socket_com,(void*)buffer)==0){
                                        //problema di ricezione con il server
                                        printf("Errore del server\n");
                                        menuPrincipale();
                                        break;
                                    }
                                    if(strncmp(buffer,"ONL\0",4)==0){   //l'utente è online
                                        char tmp[1024];
                                        sprintf(tmp,"**%s",msg);
                                        //sono sicuro abbia consegnato il messaggio
                                        printf("%s\n",tmp);
                                        printf("---------------------------\n\n");
                                        strcpy(msg,"chat_");        //riutilizzo msg per salvare il nome del file
                                        strcat(msg,user);
                                        strcat(msg,utenteChatSingola);
                                        scriviSuFile(msg,user,tmp, time(NULL));
                                        //devo stabilire la connessione con il destinatario
                                        socket_other_peer=stabilisceConnessionePeer(&other_peer, user,sd,0);
                                        if(socket_other_peer==0)    //non è stato possibile
                                            break;
                                        lastIndexSocketUtenti++;
                                        strcpy(socketUtentiChat[lastIndexSocketUtenti].user,utenteChatSingola);
                                        socketUtentiChat[lastIndexSocketUtenti].sd=socket_other_peer;
                                        FD_SET(socket_other_peer,&master); //così da poter ricevere i messaggi
                                        if(socket_other_peer>fdmax)
						                    fdmax=socket_other_peer;
                                    }else{
                                        char tmp[1024];
                                        sprintf(tmp,"* %s",msg);
                                        //non ha consegnato il messaggio perchè utente offline
                                        printf("%s\n",tmp);
                                        printf("---------------------------\n\n");
                                        strcpy(msg,"chat_");        //riutilizzo msg per salvare il nome del file
                                        strcat(msg,user);
                                        strcat(msg,utenteChatSingola);
                                        scriviSuFile(msg,user,tmp, time(NULL));
                                    }
                                }else{
                                    char tmp[1024];
                                    sprintf(tmp,"**%s",msg);
                                    //sono sicuro abbia consegnato il messaggio perchè chat diretta trai i due peer
                                    printf("%s\n",tmp);
                                    printf("---------------------------\n\n");
                                    strcpy(msg,"chat_");        //riutilizzo msg per salvare il nome del file
                                    strcat(msg,user);
                                    strcat(msg,utenteChatSingola);
                                    scriviSuFile(msg,user,tmp, time(NULL));;
                                }
                            }   
                        }
                    }else{
                        //sono nel menù principale, devo interpretare il comando
                        command= getCommand(buffer);
                        length = LENTYPE;
                        switch(command){
                            case 4: //hanging
                                if(serverOnline==0){
                                    printf("Impossibile eseguire operazione, server offline\n");
                                    break;
                                }
                                inviaStringa(sd,"HNG\0",LENTYPE);
                                inviaStringa(sd,user,strlen(user)+1);
                                while(1){
                                    if(ricevi(sd,(void*)buffer)<=0){
                                        printf("Errore del server\n");
                                        break;
                                    }
                                    if(strcmp(buffer,"END\0")==0)   //finiti gli utenti
                                        break;
                                    else{   //ho ricevuto un RTH, response to hanging
                                        // utente src
                                        ricevi(sd,(void*)buffer);
                                        printf("Utente: \"%s\" -",buffer);
                                        //numero messaggi
                                        recv(sd,(void*)&n_buf,sizeof(uint32_t),0);
                                        printf(" Messaggi: %d -",n_buf);
                                        // timestamp
                                        recv(sd,(void*)&n_buf,sizeof(uint32_t),0);
                                        //setto il timestamp in un modo leggibile
                                        t= n_buf;
                                        tm = *localtime(&t);
                                        printf(" %d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                                    }
                                    fflush(stdout);
                                }
                                printf("\n*****Sono stati mostrati tutti i messaggi*****\n");
                                menuPrincipale(); 
                                break;
                            case 5: //show
                                token=strtok(buffer, " ");
                                token = strtok(NULL, " ");
                                if(controllo_rubrica(user,token)==0 && strcmp(nomeGruppo,token)!=0){
                                    printf("Utente non presente in rubrica\n");
                                    menuPrincipale();
                                    break;
                                }
                                //oltre a mostrarli, devo anche memorizzare i messaggi nel file
                                if(serverOnline!=0){
                                    showMessage(sd,user,token,0);
                                    printf("\n*****Sono stati mostrati tutti i messaggi*****\n");
                                }
                                else
                                    printf("Impossibile eseguire operazione, server offline\n");
                                menuPrincipale();
                                break;
                            case 6: //chat
                                token=strtok(buffer, " ");
                                token = strtok(NULL, " ");
                                if(controllo_rubrica(user,token)==0 && strcmp(nomeGruppo,token)!=0){
                                    printf("Utente non presente in rubrica o gruppo inesistente\n");
                                    menuPrincipale();
                                    break;
                                }
                                //setto la var flag is_chatting
                                is_chatting=1;
                                if(strcmp(nomeGruppo,token)==0){
                                    //devo caricare lo storico dal file della chat di gruppo, setto nome file
                                    strcpy(nome_file,"chat_");
                                    strcat(nome_file,nomeGruppo);
                                    //riprendo una chat di gruppo
                                    is_group=1;
                                }else{
                                    //salvo il destinatario  
                                    strncpy(utenteChatSingola,token,strlen(token));   //non salvo \n
                                    
                                    if(strlen(nomeGruppo)==0){   //non ci sono gruppi
                                        strcpy(dest_user[0],token); //predispongo per un eventuale gruppo
                                        lastDestUser=0;
                                    }  
                                    //vedo i messaggi pendenti
                                    if(serverOnline!=0)
                                        showMessage(sd,user,token,1);

                                    //devo caricare lo storico dal file della chat singola, setto nome file
                                    strcpy(nome_file,"chat_");
                                    strcat(nome_file,user);
                                    strcat(nome_file,token);
                                    memorizzaChat(nome_file,token); //prima di aprire la chat dal file, memorizzo eventuali messaggi pendenti
                                
                                }
                                  
                                //cerco la chat nel file chat_username
                                file=fopen(nome_file,"r");
                                if(file!=NULL){
                                    //il file esiste, significa che gli utenti hanno già chattato
                                    // in precedenza e dunque devo caricare la chat
                                     memset((void*)buffer,0,1024);   //ripulisco il buffer, vorrei evitare di trovarlo sporco quando faccio altre operazioni
                                    while(feof(file)!=1){
                                        fread((void*)buffer, 10, 1, file);
                                        printf("%s",buffer);
                                    }
                                    fclose(file);
                                    memset((void*)buffer,0,1024);   //ripulisco il buffer, vorrei evitare di trovarlo sporco quando faccio altre operazioni
                                }else{
                                    printf("Nuova chat\n");
                                }
                                break;
                            case 7: // out
                                if(serverOnline!=0){
                                    inviaStringa(sd,"CLS\0",LENTYPE);  //invio al server il messaggio CLS
                                }else{
                                    //devo salvare l'istante di logout così al prossimo login lo comunico al server
                                    FILE *f = fopen(user,"w");
                                    time_t t = time(NULL);
                                    if(f!=NULL){
                                        fprintf(f,"%lu",t);     //salvo istante logout, unsigned long --> %lu
                                        fclose(f);
                                    }
                                }
                                //comunico a tutti gli utenti con cui ho una connessione aperta che mi sto disconnetendo
                                for(j=0;j<=lastIndexSocketUtenti;j++){
                                    inviaStringa(socketUtentiChat[j].sd,"CLS\0",LENTYPE);
                                }
                                exit(0);
                            default: 
                                break;
                        }
                    }
                }else{  //è arrivato qualcosa sul socket dati in ricezione
                    ret =recv(i,(void*)&length,sizeof(uint32_t),0);
                    if(ret==0){
                        if(i==sd)
                            serverOnline=0;
                        chiudiConnessioni(i,sd,socketUtentiChat,&lastIndexSocketUtenti,dest_user,&lastDestUser,is_group,nomeGruppo,&master);
                        break;
                    }
                    ret=recv(i,(void*)buffer,length,0); //type of message
                    
                   if(strncmp(buffer,"MSG\0",4)==0){        //è arrivato un messaggio da una chat singola
                        char tmp[1024];
                        memset((void*)buffer,0,sizeof(buffer));
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)buffer,length,0); //sorgente

                        if(is_chatting==0 ||(is_chatting==1 && (is_group==1 || strcmp(buffer,utenteChatSingola)!=0))){
                            printf("------Notifica-----\n");
                        }else{
                            printf("---------------------------\n");
                        }

                        if(i!=sd){ //messaggio non proviene dal server
                           for(j =0; j<=lastIndexSocketUtenti;j++){
                                if(strcmp(buffer,socketUtentiChat[j].user)==0){
                                    break;  //già presente
                                }
                                if(j==lastIndexSocketUtenti){
                                    lastIndexSocketUtenti++;
                                    strcpy(socketUtentiChat[lastIndexSocketUtenti].user,buffer);
                                    socketUtentiChat[lastIndexSocketUtenti].sd=i;
                                    break;
                                }
                            }
                        }

                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)tmp,length,0);  //destinatario
                        //sovrascrivo il destinatario perchè so che sono io
                        memset((void*)tmp,0,sizeof(tmp));
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)tmp,length,0);  //messaggio vero e proprio
                        //forza il salvataggio del messaggio in un file
                        
                        //se non sto chattando con l'utente dal quale mi arriva il messaggio
                        if(is_group==1 || is_chatting==0 || (strcmp(buffer,utenteChatSingola)!=0)){ 
                            //devo salvare il messaggio in un buffer, appena l'utente farà show/chat user, allora potrò salvare
                            salvaMessaggio(buffer,tmp);
                        }else{
                            strcpy(nome_file,"chat_");
                            strcat(nome_file,user);
                            strcat(nome_file,buffer);
                            //se sto chattando con l'utente, sono sicuro di poter scrivere perchè ho già salvato i messaggi pendenti
                            scriviSuFile(nome_file, buffer, tmp, time(NULL));
                            
                        }
                        
                        printf("Da: %s\n",buffer);
                        t = time(NULL);
                        tm = *localtime(&t);
                        printf("Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
                        printf("%s\n",tmp);
                        printf("---------------------------\n\n");

                    }else if(strncmp(buffer,"GMS\0",4)==0){ //è arrivato un messaggio da una chat di gruppo
                        char tmp[1024];
                        ricevi(i,(void*)nomeGruppo);
                        if(is_group==0){
                            //non sto chattando nella chat di gruppo
                             printf("------Notifica-----\n");
                             printf("Hai ricevuto un messaggio dal gruppo: %s\n",nomeGruppo);
                        }else
                            printf("---------------------------\n");
                        memset((void*)buffer,0,sizeof(buffer));
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)buffer,length,0); //sorgente
                        
                        printf("Da: %s\n",buffer);
                        
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)tmp,length,0);  //destinatario
                        //sovrascrivo il destinatario perchè so che sono io
                        memset((void*)tmp,0,sizeof(tmp));
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)tmp,length,0);  //messaggio vero e proprio
                        //forza il salvataggio del messaggio in un file
                        t = time(NULL);
                        tm = *localtime(&t);
                        printf("Ora: %02d-%02d-%d %02d:%02d\n",tm.tm_mday,tm.tm_mon + 1,tm.tm_year + 1900,tm.tm_hour, tm.tm_min);
                        printf("%s\n",tmp);
                        printf("---------------------------\n\n");
                        
                        //memorizzo il messaggio nello storico
                        strcpy(nome_file,"chat_");
                        strcat(nome_file,nomeGruppo);
                    }else if(strncmp(buffer,"DLV\0",LENTYPE)==0){       //mi è stato notificato che sono stati consegnati  dei messaggi
                        FILE * fp;
                        char sed[1024];
                        ricevi(i,(void*)buffer);
                        printf("------Notifica-----\n");
                        printf("Sono stati consegnati tutti i messaggi pendenti all'utente:  %s\n",buffer);
                        // nella chat dell'utente devo modificare * con **
                        //preparo la stringa del comando da eseguire
                        sprintf(sed,"sed -i 's/^* /**/g' chat_%s%s",user,buffer);
                        //eseguo il comando
                        fp = popen(sed, "r");
                        if (fp == NULL)
                            printf("Impossibile modificare chat, ma i messaggi sono stati consegnati\n");
                        pclose(fp);
                        //termine dell'esecuzione del comando
                    }else if(strncmp(buffer,"SHR\0",LENTYPE)==0 || strncmp(buffer,"GSH\0",LENTYPE)==0){
                        //condivisone di un file
                        uint32_t dim;
                        recv(i,(void*)&dim,sizeof(uint32_t),0);
                        for(j=0; j<=lastIndexSocketUtenti; j++){
                            if(socketUtentiChat[j].sd==i){
                                if(strncmp(buffer,"GSH\0",LENTYPE)==0)  //è una condivisione di gruppo
                                    riceviFile(i,(int)dim,user,socketUtentiChat[j].user,1, nomeGruppo);
                                else
                                    riceviFile(i,(int)dim,user,socketUtentiChat[j].user,0,"");
                                    
                                break;
                            }
                        }
                    }else if(strncmp(buffer,"CLS\0",LENTYPE)==0){   //qualcuno si è disconnesso
                        chiudiConnessioni(i,sd,socketUtentiChat,&lastIndexSocketUtenti,dest_user,&lastDestUser,is_group,nomeGruppo,&master);
                    }else if(strncmp(buffer,"LEV\0",LENTYPE)==0){   //utente abbandona gruppo
                        memset((void*)buffer,0,strlen(buffer));
                        ricevi(i,(void*)buffer);        //nome di chi si è disconnesso
                        j=0;
                        while(j<=lastDestUser){
                            if(strcmp(buffer,dest_user[j])==0){
                                strcpy(dest_user[j],dest_user[lastDestUser--]);
                                if(is_group==0)
                                    printf("------Notifica-----\n");
                                printf("%s ha abbandonato il gruppo\n",buffer);
                            }else
                                j++;
                        }
                        
                    }else if(strncmp(buffer,"SCN\0",LENTYPE)==0){       //qualcuno ha avviato una connessione con me
                        //start connection with...
                        memset((void*)buffer,0,sizeof(buffer));
                        recv(i,(void*)&length,sizeof(uint32_t),0);
                        recv(i,(void*)buffer,length,0);  
                        // memorizzo associazione nome utente - socket
                        lastIndexSocketUtenti++;
                        strcpy(socketUtentiChat[lastIndexSocketUtenti].user,buffer);
                        socketUtentiChat[lastIndexSocketUtenti].sd=i;
                    }else if(strncmp(buffer,"GRP\0",LENTYPE)==0){   //aggiunto ad un gruppo
                        uint32_t numeroPartecipanti=0;
                        uint32_t c=0;
                        int socket=0;
                        ricevi(i,(void*)nomeGruppo);
                        printf("Sei stato aggiunto al gruppo: '%s'\n",nomeGruppo);
                        recv(i,(void*)&numeroPartecipanti,sizeof(uint32_t),0);
                        printf("Partecipanti: ");
                        for(c=0;c<numeroPartecipanti;c++){      //ricevo il nome dei partecipanti, me compreso
                            ricevi(i,(void*)buffer);
                            if(strcmp(user,buffer)!=0){ //se il nome che ho rievuto non è il mio
                                int portaInt;
                                lastDestUser++;
                                strcpy(dest_user[lastDestUser],buffer); //aggiungo username a partecipanti gruppo
                                socket = cercaSocketUtente(buffer,socketUtentiChat,lastIndexSocketUtenti); //cerco se ho già una connessione attiva con quel utente
                                if(socket==0){
                                    //non mi ci sono mai connesso e mi ci devo connettere
                                    if(serverOnline==0){
                                        printf("Impossibile eseguire operazione, server offline\n");
                                        break;
                                    }
                                    portaInt = chiediPorta(sd,buffer);      //ottengo la porta
                                    socket=stabilisceConnessionePeer(&other_peer, user,sd,htons(portaInt)); //apro connessione tcp
                                    if(socket==0){
                                        printf("Impossibile avviare chat di gruppo\n");
                                        break;
                                    }
                                    lastIndexSocketUtenti++;        // aggiungo l'associazione username - socket
                                    strcpy(socketUtentiChat[lastIndexSocketUtenti].user,buffer);
                                    socketUtentiChat[lastIndexSocketUtenti].sd=socket;
                                    FD_SET(socket,&master); //così da poter ricevere i messaggi
                                    if(socket>fdmax)
						                fdmax=socket;
                                }
                            }
                            printf("%s ",buffer);
                        }
                        printf("\n");
                    }
                }
        
            }
        }
        
    }
}   

