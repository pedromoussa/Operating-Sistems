#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define STATUS_NEW 1
#define STATUS_READY 2
#define STATUS_RUNNING 3
#define STATUS_BLOCKED_DISK 4
#define STATUS_BLOCKED_TAPE 5
#define STATUS_BLOCKED_PRINTER 6
#define STATUS_DONE 7

#define HIGH_PRIORITY 1
#define LOW_PRIORITY 0

/*
 *Tipo de IO
 */
#define DISK 0
#define TAPE 1
#define PRINTER 2

#define NUM_IO_TYPES 3

/*
 *Duração do IO
 */
#define DISK_TIME 2
#define TAPE_TIME 3
#define PRINTER_TIME 4

#define MAX_ARRIVAL_TIME 10
#define MAX_SERVICE_TIME 15

#define TEMPO_QUANTUM 4

#define NUM_MAX_PROCESSES 5

/*
 * Informações necessárias para o SO gerenciar os processos.
 *
 * status: um entre new, ready, running, blocked_disk, blocked_tape, blocked_printer, done.
 * processed_time: tempo total que o processo já passou sendo executado.
 * time_slice: tempo que o processo já passou sendo executado durante a fatia de tempo atual.
 * t_chegada: quando o processo chega no SO.
 * t_servico: tempo total que o processo deve ser executado para terminar.
 */
typedef struct {
    int process_id, status, priority, processed_time, time_slice, t_chegada, t_servico;
} process_control_block;

/*
 * t_IO: vetor contendo os tempos nos quais o processo vai requisitar uma operação de I/O ao SO.
 *       Deve ser mantido ordenado para funcionar corretamente.
 * IO_type: vetor contendo os tipos de operação de I/O (disk, tape, printer) que serão requisitadas
 *          nos instantes de tempo contidos no vetor t_IO.  
 * remainingIOOperations: número de operações de I/O que ainda serão requisitadas pelo processo.
 */
typedef struct {
    int* t_IO, *IO_type;
    int remainingIOOperations, numIOOperations;
    process_control_block pcb;
} process;

/*
 * Implementa uma fila circular de processos.
 *
 * queue: vetor de ponteiros para processos.
 * frontIndex: indica a posição da fila contendo o primeiro elemento da fila.
 * rearIndex: indica a primeira posição livre após frontIndex.
 * length: tamanho atual da fila.
 * maxLength: tamanho máximo da fila.
 */
typedef struct {
    int frontIndex, rearIndex, length, maxLength;
    process** queue;
} queue;

/***************************** GLOBAL VARIABLES ********************************/

queue newProcesses, highPriorityQueue, lowPriorityQueue, diskQueue, tapeQueue, printerQueue;
int instanteTempoGlobal = 0; // instante de tempo atual para todo o SO
int processesDoneCounter = 0, turnaround_time, idleCPUtime = 0, turnaround_times_sum = 0;
double average_turnaround_time = 0;
int disk_timer = 0, tape_timer = 0, printer_timer = 0; // usados para contar quanto tempo o processo utilizando o dispositivo de I/O já passou nele
int anyProcessRunning = 0;

/***************************** FUNCTIONS ********************************/

int checkForRepeatedElements(int* array, int size);
int* randomArrayWithUniqueElements(int size, int maxValue);
void bubbleSort(int* array, int size);
int* randomArray(int size, int maxValue);
process* newProcess(int id, int t_chegada, int t_servico, int status, int priority, int remainingIOOperations, int* t_IO, int* IO_type);
process* newRandomProcess(int id);
process* getProcess();
void executeProcess(process *p);
void printProcessInfo(process* p);
void printProcessIO(process* p);
void initializeQueue(queue* q);
void terminateQueue(queue q);
int isEmpty(queue q);
int isFull(queue q);
void push(queue* q, process* p);
process* pop(queue* q);
void swap(process* p1, process* p2);
void sortQueue(queue* q);
void printQueue(queue* q);
int hasIORequest(process* p);
void sendToIOQueue(process* p);
int newProcessHasArrived();
void manageNewProcesses();
void executeIOOperations();
void terminateProcess(process* p);
void makeProcesses();
void printNewProcesses();
void red();
void green();
void blue();
void yellow();
void cyan();
void purple();
void reset();

/***************************** UTILITY FUNCTIONS ********************************/

void red() {
	printf("\033[1;31m");
}

void green() {
	printf("\033[0;32m");
}

void blue() {
	printf("\033[0;34m");
}

void yellow() {
	printf("\033[1;33m");
}

void cyan() {
	printf("\033[0;36m");
}

void purple() {
	printf("\033[0;35m");
}

void reset() {
	printf("\033[0m");
}

/*
 * Checa se todos os elementos em um array sao unicos.
 * Returna 0 se todos sao unicos, 1 se existem elementos repetidos.
 */
int checkForRepeatedElements(int* array, int size) {
    int anyEquality = 0;
	
	for(int i = 0; i < size; i++)
		for(int j = i+1; j < size; j++)
			if(array[i] == array[j])
				anyEquality = 1;
	
	return anyEquality == 0? 0 : 1;
}

/*
 * Cria um vetor aleatorio sem elementos repetidos
 * Gera um novo vetor com elementos aleatorios ate que gere um onde todos os elementos sao unicos
 * Usada para gerar tempos dos pedidos de i/o, para impedir que um processo realize dois pedidos de i/o simultaneamente
 */
int* randomArrayWithUniqueElements(int size, int maxValue) {
    int* array = (int *) malloc(sizeof(int)*size);
    
    //usado para garantir que todos elementos sao distintos
    int equalElementsInArray = 1;

	while(equalElementsInArray) {
		for(int i = 0; i < size; i++)
			array[i] = (rand()%(maxValue-1))+1;
		
		if(size == 1) 
			equalElementsInArray = 0;
		else
			equalElementsInArray = checkForRepeatedElements(array, size);
	}

    return array;
}

void bubbleSort(int* array, int size) {
	int temp;
	
	for(int i = 0; i < size; i++)
		for(int j = i+1; j < size; j++)
			if(array[i] > array[j]) {
				temp = array[i];
				array[i] = array[j];
				array[j] = temp;
			}
}

/*
 * Gera um vetor aleatorio onde os elementos podem ser repetidos.
 * Usado para decidir os tipos de operacoes de I/O que um processo realiza.
 */
int* randomArray(int size, int maxValue) {
	int* array = (int *) malloc(sizeof(int)*size);
	
	for(int i = 0; i < size; i++)
		  array[i] = rand()%maxValue;
	
	return array;
}

/***************************** PROCESS FUNCTIONS ********************************/

/*
 * Um novo processo sempre é inicializado com status = STATUS_NEW (1).
 * Um novo processo sempre tem alta prioridade (HIGH_PRIORITY).
 * Os tempos de chegada e serviço sao decididos aleatoriamente, assim como a quantidade, tempo 
 * e tipo das operações de I/O.
 */
process* newRandomProcess(int id) {
    process* p = (process*) malloc(sizeof(process));

    p->pcb.t_chegada = rand()%MAX_ARRIVAL_TIME;
    p->pcb.t_servico = rand()%MAX_SERVICE_TIME+1; //processo leva ao menos 1 unidade de tempo
    
    // um processo realiza um maximo de 3 operacoes de i/o, ou o tempo de serviço do 
    // processo menos 1 se o tempo de serviço for menor do que 3
    int MAX_IO_OPS;
    MAX_IO_OPS = (p->pcb.t_servico > 3? 3 : p->pcb.t_servico-1);
	if(MAX_IO_OPS > 0)
    	p->remainingIOOperations = rand()%MAX_IO_OPS;
    else
		p->remainingIOOperations = 0;

    if(p->remainingIOOperations != 0) {
		p->t_IO = randomArrayWithUniqueElements(p->remainingIOOperations, p->pcb.t_servico);
		bubbleSort(p->t_IO, p->remainingIOOperations); // ordena o vetor de tempos de requisições de I/O para simplificar o gerenciamento
		p->IO_type = randomArray(p->remainingIOOperations, NUM_IO_TYPES);
    }

	p->numIOOperations = p->remainingIOOperations;
    p->pcb.process_id = id;
    p->pcb.status = STATUS_NEW;
    p->pcb.priority = HIGH_PRIORITY;
    p->pcb.processed_time = 0;
	p->pcb.time_slice = 0;

    return p;
}

/* 
 * Gera um novo processo.
 * O usuário deve fornecer o vetor de tempos de requisições de I/O já ordenado.
 */ 
process* newProcess(int id, int t_chegada, int t_servico, int status, int priority, int remainingIOOperations, int* t_IO, int* IO_type) {
    process* p = (process*) malloc(sizeof(process));

    p->pcb.t_chegada = t_chegada;
    p->pcb.t_servico = t_servico;

    if(p->remainingIOOperations != 0) {
		p->t_IO = t_IO;
		p->IO_type = IO_type;
    }

	p->numIOOperations = p->remainingIOOperations;
    p->pcb.process_id = id;
    p->pcb.status = status;
    p->pcb.priority = priority;
    p->pcb.processed_time = 0;
	p->pcb.time_slice = 0;

    return p;
}

/*
 * Retorna o processo pronto na frente da fila de maior prioridade, ou de
 * menor prioridade caso somente esta esteja disponivel. Caso nenhuma esteja,
 * retorna NULL.
 */
process* getProcess() {
	process* p = NULL;
	cyan();
	printf(" \n-----------------     Get Process      ---------------\n");
	reset();

	if(!isEmpty(highPriorityQueue)) {
		printf("Fila de Alta Prioridade\n");
		anyProcessRunning = 1;
		p = pop(&highPriorityQueue);
		p->pcb.status = STATUS_RUNNING;
		return p;
	}
	else if(!isEmpty(lowPriorityQueue)){
		printf("Fila de Baixa Prioridade\n");
		anyProcessRunning = 1;
		p = pop(&lowPriorityQueue);
		p->pcb.status = STATUS_RUNNING;
	 	return p;
	} else {
		red();
		printf("Sem Processos com STATUS READY\n");
		reset();
	}

	idleCPUtime++;
	return p;
}

/*
 * Checa se algum processo chega na cpu no instante atual.
 * Se sim, retorna 1, caso contrário, retorna 0.
 */
int newProcessHasArrived() {
	if(!isEmpty(newProcesses) && newProcesses.queue[newProcesses.frontIndex]->pcb.t_chegada == instanteTempoGlobal)
		return 1;
	return 0;
}

/*
 * Transfere processos que acabaram de chegar na cpu para filas adequadas.
 * 'while' é usado ao invés de 'if' pois vários processos podem chegar em um 
 * mesmo instante.
 */
void manageNewProcesses() {
	while(newProcessHasArrived()) {
		yellow();
		printf("\nProcesso %d com tempo de chegada %d passou para \nFila de Alta Prioridade no instante %d\n", newProcesses.queue[newProcesses.frontIndex]->pcb.process_id, newProcesses.queue[newProcesses.frontIndex]->pcb.t_chegada, instanteTempoGlobal);
		reset();
		newProcesses.queue[newProcesses.frontIndex]->pcb.status = STATUS_READY;
		push(&highPriorityQueue, pop(&newProcesses));
	}
}

/*
 * Verifica se processo faz uma requisição de I/O no instante atual.
 * Se faz, retorna 1, caso contrário, retorna 0.
 */
int hasIORequest(process* p) {
	return (p->remainingIOOperations > 0 && p->t_IO[0] == p->pcb.processed_time);
}

/*
 * Envia o processo para a fila de I/O correta e o bloqueia.
 */
void sendToIOQueue(process* p) {
	yellow();
	if(p->IO_type[0] == DISK) {
		p->pcb.status = STATUS_BLOCKED_DISK;
		printf("Processo %d entrou na Fila de IO-Disk\n", p->pcb.process_id);
		push(&diskQueue, p);
		anyProcessRunning = 0;
	} else if(p->IO_type[0] == TAPE) {
		p->pcb.status = STATUS_BLOCKED_TAPE;
		printf("Processo %d entrou na Fila de IO-Tape\n", p->pcb.process_id);
		push(&tapeQueue, p);
		anyProcessRunning = 0;
	} else if(p->IO_type[0] == PRINTER) {
		p->pcb.status = STATUS_BLOCKED_PRINTER;
		printf("Processo %d entrou na Fila de IO-Printer\n", p->pcb.process_id);
		push(&printerQueue, p);
		anyProcessRunning = 0;
	}
	reset();
	p->remainingIOOperations--;
	p->t_IO++;
	p->IO_type++;
}

/*
 * Simula a execucao de um processo, atualizando seu tempo gasto executando, 
 * o preemtando para a fila correta caso seu time slice termine / requira uma
 * operacao de IO, ou o terminando caso termine de executar.
 */
void executeProcess(process* p) {
	cyan();
	printf(" \n-----------------   Execute Process    ---------------\n");
	reset();
	printf("ID do Processo: %d\n", p->pcb.process_id);
	printf("Tempo processado: %d\n", p->pcb.processed_time);
	printf("Tempo restante de execução: %d\n", p->pcb.t_servico - p->pcb.processed_time);

	p->pcb.time_slice++;

	// printf("Time Slice do Processo: %d\n", p->pcb.time_slice);
	if(p->pcb.time_slice % TEMPO_QUANTUM == 0 && p->pcb.t_servico != p->pcb.processed_time) {

		p->pcb.status = STATUS_READY;
		p->pcb.priority = LOW_PRIORITY;
		push(&lowPriorityQueue, p);
		yellow();
		printf("\nProcesso %d passou para Fila de Baixa Prioridade \nno instante %d após %d instantes de execução\n", p->pcb.process_id, instanteTempoGlobal, p->pcb.time_slice);
		reset();
		anyProcessRunning = 0;
		p->pcb.time_slice = 0;

	} else if(p->pcb.processed_time == p->pcb.t_servico) {

		green();
		printf("Processo terminou de executar\n");
		turnaround_time = (instanteTempoGlobal+1) - p->pcb.t_chegada;
		turnaround_times_sum += turnaround_time;
		printf("Tempo de Turnaround: %d\n", turnaround_time);
		p->pcb.status = STATUS_DONE;
		processesDoneCounter++;
		anyProcessRunning = 0;
		terminateProcess(p);
		reset();

	}
}

/*
 * Simula o tempo gasto nas operações de I/O para os processos que estejam os usando, além de retornar
 * eles para as filas corretas. Foi assumido que existe somente um dispositivo capaz de realizar cada tipo
 * de operação de I/O, isto é, somente existem 1 disco, 1 fita, e 1 impressora. Também foi assumido que 
 * cada um desses aparelhos só é capaz de ser utilizado por um processo de cada vez.
 */
void executeIOOperations() {
	process* p;
	cyan();
	printf("\n----------------- Execute IO Operation ---------------\n");
	reset();
	if(isEmpty(printerQueue) && isEmpty(tapeQueue) && isEmpty(diskQueue)) {
		red();
		printf("Nenhum Processo executando operação de IO\n");
		reset();
	}
	if(!isEmpty(printerQueue)) {
		printf("Processo %d executa IO: Printer\n", printerQueue.queue[printerQueue.frontIndex]->pcb.process_id);
		if(++printer_timer == PRINTER_TIME) { // se processo gastou tempo o suficiente para completar a operação
			p =	pop(&printerQueue);
			p->pcb.priority = HIGH_PRIORITY;
			p->pcb.status = STATUS_READY;
			push(&highPriorityQueue, p);
			printer_timer = 0;
			yellow();
			printf("Processo %d saiu da Fila de IO-Printer para Fila de Alta Prioridade\n", p->pcb.process_id);
			reset();
		} else { 
			printf("Tempo restante da operação de IO-Printer: %d\n", PRINTER_TIME - printer_timer);
		}
	}
	if(!isEmpty(tapeQueue)) {
		printf("Processo %d executa IO: Tape\n", tapeQueue.queue[tapeQueue.frontIndex]->pcb.process_id);
		if(++tape_timer == TAPE_TIME) {
			p =	pop(&tapeQueue);
			p->pcb.priority = HIGH_PRIORITY;
			p->pcb.status = STATUS_READY;
			push(&highPriorityQueue, p);
			tape_timer = 0;
			yellow();
			printf("Processo %d saiu da Fila de IO-Tape para Fila de Alta Prioridade\n", p->pcb.process_id);
			reset();
		} else { 
			printf("Tempo restante da operação de IO-Tape: %d\n", TAPE_TIME - tape_timer);
		}
	}
	if(!isEmpty(diskQueue)) {
		printf("Processo %d executa IO: Disk\n", diskQueue.queue[diskQueue.frontIndex]->pcb.process_id);
		if(++disk_timer == DISK_TIME) {
			p =	pop(&diskQueue);
			p->pcb.priority = LOW_PRIORITY;
			p->pcb.status = STATUS_READY;
			push(&lowPriorityQueue, p);
			disk_timer = 0;
			yellow();
			printf("Processo %d saiu da Fila de IO-Disk para Fila de Baixa Prioridade\n", p->pcb.process_id);
			reset();
		} else { 
			printf("Tempo restante da operação de IO-DISK: %d\n", DISK_TIME - disk_timer);
		}
	}
}

/*
 * Libera a memória armazenada pelo processo.
 */
void terminateProcess(process* p) {
	if(p != NULL) {
		p->t_IO = p->t_IO - p->numIOOperations;
		if(p->t_IO != NULL){
			free(p->t_IO);
		}
		p->IO_type = p->IO_type - p->numIOOperations;
		if(p->IO_type != NULL) {
			free(p->IO_type);
		}
		free(p);
	}
}

/*
 * Utilizada durante testes.
 */
void printProcessInfo(process* p) {
    printf("\n============================\n");
    printf("Information of process #%d:\n", p->pcb.process_id);
    printf("Arrival time: %d\n", p->pcb.t_chegada);
    printf("Service time: %d\n", p->pcb.t_servico);
    printf("Status: NEW\n");
    printf("Priority: HIGH PRIORITY\n");
    printf("Numero de operacoes de I/O: %d\n", p->remainingIOOperations);
}

/*
 * Imprime informações relacionadas a I/O de um processo. 
 * Utilizada durante testes.
 */
void printProcessIO(process* p) {
	if(p->remainingIOOperations) { 
		printf("\nI/O of process #%d:\n", p->pcb.process_id);
		for(int i = 0; i < p->remainingIOOperations; i++) {
			printf("Time of I/O %d: %d \n", i, p->t_IO[i]);
			if(p->IO_type[i] == 0) {
				printf("Type of I/O %d: Disk \n", i);
			} else if (p->IO_type[i] == 1) {
				printf("Type of I/O %d: Tape \n", i);
			} else {
				printf("Type of I/O %d: Printer \n", i);
			}
		}
	}
}

// cria novos processos, os insere em newProcesses e os imprime
void makeProcesses() {
    for(int i = 0; i < NUM_MAX_PROCESSES; i++) {
        push(&newProcesses, newRandomProcess(i));
	}
    sortQueue(&newProcesses);
}

// imprime informacoes sobre os processos criados
void printNewProcesses() {
	for(int i = 0; i < NUM_MAX_PROCESSES; i++) { 
        printProcessInfo(newProcesses.queue[i]);
		printProcessIO(newProcesses.queue[i]);
	}
}

/***************************** QUEUE FUNCTIONS ********************************/

void initializeQueue(queue* q) {
	q->queue = (process**) malloc(NUM_MAX_PROCESSES*sizeof(process*));
	q->frontIndex = 0;
	q->rearIndex = 0;
	q->length = 0;
	q->maxLength = NUM_MAX_PROCESSES;
}

int isEmpty(queue q) {
	return q.length == 0? 1 : 0;
}

int isFull(queue q) {
	return q.length == q.maxLength? 1 : 0;
}

/*
 * Insere o processo p na posição indicada pelo rearIndex, e atualiza este e o 
 * tamanho da fila. Caso a fila já esteja cheia, não faz nada.
 */
void push(queue* q, process* p) {
	if(!isFull(*q)) {
		q->queue[q->rearIndex] = p;
		q->rearIndex = (q->rearIndex+1)%q->maxLength;
		q->length++;
	}
}

/*
 * Remove o primeiro processo da fila e o retorna.
 * Se a fila estava vazia antes da chamada, simplesmente retorna NULL.
 */
process* pop(queue* q) {
	if(!isEmpty(*q)) {
		process* removedProcess = q->queue[q->frontIndex];
		q->queue[q->frontIndex] = NULL;
		q->frontIndex = (q->frontIndex+1)%q->maxLength;
		q->length--;
		return removedProcess;
	}
	return NULL;
}

void swap(process* p1, process* p2) {
	process temp;
	temp = *p1;
	*p1 = *p2;
	*p2 = temp;
}

/*
 * Ordena a fila de forma crescente com base nos tempos de chegada dos processos
 * ao sistema operacional.
 * Usado para ordenar o vetor de novos processos, para simplificar a transferência 
 * de novos processos para a fila de alta prioridade.
 */
void sortQueue(queue* q) {
	int index1, index2;
	
	for(int i = 0; i < q->length; i++) {
		for(int j = i+1; j < q->length; j++) {
			index1 = (q->frontIndex + i) % q->maxLength;
			index2 = (q->frontIndex + j) % q->maxLength;
			if(q->queue[index1]->pcb.t_chegada > q->queue[index2]->pcb.t_chegada)
				swap(q->queue[index1], q->queue[index2]);
		}
	}
}

/*
 * Libera toda a memória armazenada para a fila.
 */
void terminateQueue(queue q) {
	for(int i = 0; i < q.maxLength; i++) {
		if(q.queue[i] != NULL)
    	free(q.queue[i]);
  	}
  	free(q.queue);
}

/*
 * Imprime os processos em uma fila, do primeiro ao último.
 */
void printQueue(queue *q) {
  	for(int i = q->frontIndex; i != q->rearIndex; i = (i+1)%q->maxLength) {
    	printf("| %d ", q->queue[i]->pcb.process_id);
  	}
  	puts("");
}
/***************************** SCHEDULER ***************************/

/*
 * Simulacao do escalonador de processos
 */
void scheduler() {
	
	int allProcessesDone = 0;
	process* executingProcess;

	while(!allProcessesDone) {

		printf("\n\n================= CURRENT TIME UNIT: %d ============== \n", instanteTempoGlobal);

		manageNewProcesses();

		executeIOOperations();

		if(!anyProcessRunning) { // se a cpu nao esta executando nenhum processo
			executingProcess = getProcess();
			if(executingProcess != NULL)
				printf("Recebeu Processo de ID: %d\n", executingProcess->pcb.process_id);
		}

		if(executingProcess != NULL) {
			executingProcess->pcb.processed_time++;
			if(hasIORequest(executingProcess)) {
				executingProcess->pcb.time_slice = 0;
				sendToIOQueue(executingProcess);
			} else {
				executeProcess(executingProcess);
			}
		}

		if(processesDoneCounter == NUM_MAX_PROCESSES) {
			green();
			puts("\nTodos processos terminaram de executar\n");
			reset();
			allProcessesDone = 1; 
		}

		instanteTempoGlobal++;

		if(isEmpty(highPriorityQueue) && isEmpty(lowPriorityQueue) && isEmpty(printerQueue) && isEmpty(tapeQueue) && isEmpty(diskQueue)) {
		} else {
			cyan();
			printf("\n-----------------       Queues       -----------------\n");
			purple();
			if(!isEmpty(highPriorityQueue)) {
				printf("Fila de Alta Prioridade:  ");
				printQueue(&highPriorityQueue);
			}
			if(!isEmpty(lowPriorityQueue)) {
				printf("Fila de Baixa Prioridade: ");
				printQueue(&lowPriorityQueue);
			}
			if(!isEmpty(printerQueue)) {
				printf("Fila de IO-Printer:       ");
				printQueue(&printerQueue);
			}
			if(!isEmpty(tapeQueue)) {
				printf("Fila de IO-Tape:          ");
				printQueue(&tapeQueue);
			}
			if(!isEmpty(diskQueue)) {
				printf("Fila de IO-Disk:          ");
				printQueue(&diskQueue);
			}
			reset();
		}
    }

}

/***************************** MAIN ********************************/

int main(int argc, char** argv) {

    initializeQueue(&newProcesses); // a fila newProcesses armazena os processos enquanto eles ainda não chegaram na cpu pela primeira vez
    initializeQueue(&highPriorityQueue);
    initializeQueue(&lowPriorityQueue);
    initializeQueue(&diskQueue);
    initializeQueue(&tapeQueue);
    initializeQueue(&printerQueue);
    
    srand(time(NULL)); //para gerar os parametros de cada processo aleatoriamente
    
	makeProcesses();
	printNewProcesses();

	scheduler();

	cyan();
	printf("----------------  Simulation Metrics  ----------------\n");
	green();

	printf("A simulação levou %d unidades de tempo\n", instanteTempoGlobal);

	printf("%d processos foram executados\n", processesDoneCounter);

	average_turnaround_time = (double)turnaround_times_sum / NUM_MAX_PROCESSES;

	printf("Tempo de Turnaround médio: %.2lf\n", average_turnaround_time);
	printf("Tempo de CPU osciosa: %d\n", idleCPUtime);
	printf("Utilização da CPU: %.2lf%c\n\n", 100*(instanteTempoGlobal - idleCPUtime)/(double)instanteTempoGlobal, 37);
	reset();

	terminateQueue(highPriorityQueue);
	terminateQueue(lowPriorityQueue);
	terminateQueue(diskQueue);
	terminateQueue(tapeQueue);
	terminateQueue(printerQueue);

    return 0;

}