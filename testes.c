#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
 
#include "sync_car.h"
 
static atomic_int liberados;
static atomic_int chamaram_on_board;
static int falhas = 0;
 
/* ------------------------------------------------------------------ */
/* utilitarios                                                         */
/* ------------------------------------------------------------------ */
 
static void check(int cond, const char *fmt, ...)
{
	va_list ap;
 
	if (cond)
		return;
	falhas++;
	printf("    [FALHA] ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}
 
static void on_alarm(int sig)
{
	(void)sig;
	_exit(2); /* o processo pai interpreta o codigo 2 como timeout */
}
 
static void armar(unsigned segundos)
{
	alarm(segundos);
}
 
static double agora_ms(void)
{
	struct timespec t;
 
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
 
/* Espera (lendo o estado sob o lock) ate n passageiros estarem dormindo. */
static void esperar_fila(struct station *st, int n)
{
	for (;;) {
		pthread_mutex_lock(&st->lock);
		int w = st->passageiros_esperando;
		pthread_mutex_unlock(&st->lock);
		if (w == n)
			return;
		usleep(200);
	}
}
 
/* ------------------------------------------------------------------ */
/* grupo de passageiros (threads)                                      */
/* ------------------------------------------------------------------ */
 
struct arg_passageiro {
	struct station *st;
	unsigned atraso_max_us;
	unsigned semente;
};
 
struct grupo {
	pthread_t *tids;
	struct arg_passageiro *args;
	int n, cap;
};
 
static void *passageiro(void *p)
{
	struct arg_passageiro *a = p;
 
	station_wait_for_car(a->st);
	atomic_fetch_add(&liberados, 1);
	if (a->atraso_max_us)
		usleep(rand_r(&a->semente) % (a->atraso_max_us + 1));
	atomic_fetch_add(&chamaram_on_board, 1);
	station_on_board(a->st);
	return NULL;
}
 
static void grupo_init(struct grupo *g, int cap)
{
	g->tids = calloc(cap > 0 ? cap : 1, sizeof(pthread_t));
	g->args = calloc(cap > 0 ? cap : 1, sizeof(struct arg_passageiro));
	g->n = 0;
	g->cap = cap;
}
 
static void grupo_add(struct grupo *g, struct station *st, unsigned atraso_max_us)
{
	pthread_attr_t at;
	struct arg_passageiro *a = &g->args[g->n];
 
	a->st = st;
	a->atraso_max_us = atraso_max_us;
	a->semente = 12345u + (unsigned)g->n * 7919u;
 
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, 256 * 1024);
	if (pthread_create(&g->tids[g->n], &at, passageiro, a) != 0) {
		perror("pthread_create");
		exit(1);
	}
	pthread_attr_destroy(&at);
	g->n++;
}
 
static void grupo_join(struct grupo *g)
{
	for (int i = 0; i < g->n; i++)
		pthread_join(g->tids[i], NULL);
	free(g->tids);
	free(g->args);
}
 
/* ------------------------------------------------------------------ */
/* cenarios deterministicos                                            */
/* ------------------------------------------------------------------ */
 
struct caso {
	const char *nome;
	int passageiros;
	int assentos;
	unsigned atraso_us; /* atraso maximo de cada passageiro antes de on_board */
};
 
/*
 * Todos os passageiros ja estao dormindo quando o carro chega. Entao o
 * resultado esperado e exato: saem min(passageiros, assentos).
 */
static int cenario_basico(const void *p)
{
	const struct caso *c = p;
	struct station st;
	struct grupo g;
	int esperado = c->passageiros < c->assentos ? c->passageiros : c->assentos;
 
	station_init(&st);
	grupo_init(&g, c->passageiros);
	armar(15);
 
	for (int i = 0; i < c->passageiros; i++)
		grupo_add(&g, &st, c->atraso_us);
	esperar_fila(&st, c->passageiros);
 
	double t0 = agora_ms();
	station_load_car(&st, c->assentos);
	double dt = agora_ms() - t0;
	int ao_sair = atomic_load(&chamaram_on_board);
 
	usleep(30000); /* da tempo de um passageiro indevido escapar, se houver bug */
	int lib = atomic_load(&liberados);
 
	pthread_mutex_lock(&st.lock);
	int esp = st.passageiros_esperando;
	int livres = st.quantidade_assentos_livres;
	int emb = st.passageiros_embarcando;
	pthread_mutex_unlock(&st.lock);
 
	check(ao_sair == esperado,
	      "ao sair, %d passageiros tinham chamado on_board (esperado %d)",
	      ao_sair, esperado);
	check(lib == esperado,
	      "%d passageiros liberados (esperado %d)", lib, esperado);
	check(esp == c->passageiros - esperado,
	      "%d passageiros ainda esperando (esperado %d)",
	      esp, c->passageiros - esperado);
	check(livres == 0,
	      "carro saiu deixando quantidade_assentos_livres = %d (esperado 0)",
	      livres);
	check(emb == 0,
	      "carro saiu com passageiros_embarcando = %d (esperado 0)", emb);
	if (esperado == 0)
		check(dt < 200.0,
		      "station_load_car demorou %.0f ms (deveria retornar na hora)", dt);
 
	if (falhas)
		return falhas;
 
	/* limpeza: um segundo carro leva quem sobrou */
	int restantes = c->passageiros - esperado;
	if (restantes > 0) {
		station_load_car(&st, restantes);
		check(atomic_load(&chamaram_on_board) == c->passageiros,
		      "segundo carro: %d de %d passageiros embarcaram",
		      atomic_load(&chamaram_on_board), c->passageiros);
	}
	grupo_join(&g);
 
	pthread_mutex_lock(&st.lock);
	check(st.passageiros_esperando == 0 && st.quantidade_assentos_livres == 0 &&
	      st.passageiros_embarcando == 0,
	      "estado final inconsistente: esperando=%d livres=%d embarcando=%d",
	      st.passageiros_esperando, st.quantidade_assentos_livres,
	      st.passageiros_embarcando);
	pthread_mutex_unlock(&st.lock);
	return falhas;
}
 
/* ------------------------------------------------------------------ */
/* chegada tardia: passageiros chegam com o carro ja na estacao        */
/* ------------------------------------------------------------------ */
 
struct arg_carro {
	struct station *st;
	int assentos;
};
 
static void *thread_carro(void *p)
{
	struct arg_carro *a = p;
 
	station_load_car(a->st, a->assentos);
	return NULL;
}
 
static int cenario_chegada_tardia(const void *unused)
{
	(void)unused;
	struct station st;
	struct grupo g;
	pthread_t car;
	struct arg_carro ac;
 
	station_init(&st);
	grupo_init(&g, 7);
	armar(15);
 
	for (int i = 0; i < 3; i++)
		grupo_add(&g, &st, 100000); /* on_board demora ate 100 ms */
	esperar_fila(&st, 3);
 
	ac.st = &st;
	ac.assentos = 10;
	pthread_create(&car, NULL, thread_carro, &ac);
	usleep(20000); /* carro ja esta na estacao esperando o embarque */
	for (int i = 0; i < 4; i++)
		grupo_add(&g, &st, 100000);
	pthread_join(car, NULL);
 
	int cham = atomic_load(&chamaram_on_board);
	int lib = atomic_load(&liberados);
	printf("    info: %d dos 4 retardatarios entraram no carro que ja estava na estacao\n",
	       lib - 3);
 
	check(lib >= 3 && lib <= 7, "%d passageiros liberados (esperado entre 3 e 7)", lib);
	check(cham == lib,
	      "carro saiu com %d liberados mas so %d chamaram on_board", lib, cham);
	if (falhas)
		return falhas;
 
	int restantes = 7 - lib;
	if (restantes > 0) {
		esperar_fila(&st, restantes);
		station_load_car(&st, restantes);
	}
	check(atomic_load(&chamaram_on_board) == 7,
	      "no total, %d de 7 passageiros embarcaram", atomic_load(&chamaram_on_board));
	grupo_join(&g);
	return falhas;
}
 
/* ------------------------------------------------------------------ */
/* estresse: muitos passageiros e carros, tudo com tempos aleatorios   */
/* ------------------------------------------------------------------ */
 
struct cfg_estresse {
	int passageiros;
	int max_assentos;
	unsigned atraso_on_board_us;
	unsigned semente;
};
 
struct arg_chegadas {
	struct grupo *g;
	struct station *st;
	int n;
	unsigned atraso;
	unsigned semente;
};
 
static void *thread_chegadas(void *p)
{
	struct arg_chegadas *a = p;
 
	for (int i = 0; i < a->n; i++) {
		grupo_add(a->g, a->st, a->atraso);
		if (rand_r(&a->semente) % 4 == 0)
			usleep(rand_r(&a->semente) % 500);
	}
	return NULL;
}
 
/*
 * Invariantes verificados a cada carro:
 *  1. nunca sai mais gente do que assentos
 *  2. todo passageiro liberado ja chamou on_board quando o carro retorna
 *  3. ninguem e liberado enquanto nao ha carro na estacao
 * No final: todos embarcaram e o estado da estacao voltou a zero.
 */
static int cenario_estresse(const void *p)
{
	const struct cfg_estresse *c = p;
	struct station st;
	struct grupo g;
	pthread_t spawner;
	struct arg_chegadas ch;
	unsigned sem = c->semente ^ 0x9e3779b9u;
	int carros = 0;
 
	station_init(&st);
	grupo_init(&g, c->passageiros);
	armar(60);
 
	ch.g = &g;
	ch.st = &st;
	ch.n = c->passageiros;
	ch.atraso = c->atraso_on_board_us;
	ch.semente = c->semente;
	pthread_create(&spawner, NULL, thread_chegadas, &ch);
 
	while (atomic_load(&chamaram_on_board) < c->passageiros && carros < 200000) {
		int assentos = rand_r(&sem) % (c->max_assentos + 1);
		int r0 = atomic_load(&liberados);
		int b0 = atomic_load(&chamaram_on_board);
 
		station_load_car(&st, assentos);
 
		int r1 = atomic_load(&liberados);
		int b1 = atomic_load(&chamaram_on_board);
 
		check(r1 - r0 <= assentos,
		      "carro %d: %d passageiros liberados para %d assentos",
		      carros, r1 - r0, assentos);
		check(b1 - b0 == r1 - r0,
		      "carro %d saiu cedo: %d liberados mas so %d chamaram on_board",
		      carros, r1 - r0, b1 - b0);
 
		usleep(rand_r(&sem) % 1000);
		check(atomic_load(&liberados) == r1,
		      "carro %d: passageiro liberado sem carro na estacao", carros);
 
		if (falhas)
			return falhas; /* encerra o processo filho de imediato */
		carros++;
		if (rand_r(&sem) % 3 == 0)
			usleep(rand_r(&sem) % 300);
	}
 
	pthread_join(spawner, NULL);
	check(atomic_load(&chamaram_on_board) == c->passageiros,
	      "nao terminou: %d de %d passageiros embarcaram",
	      atomic_load(&chamaram_on_board), c->passageiros);
	if (falhas)
		return falhas;
 
	grupo_join(&g);
	check(atomic_load(&liberados) == c->passageiros,
	      "%d liberados no total (esperado %d)", atomic_load(&liberados), c->passageiros);
 
	pthread_mutex_lock(&st.lock);
	check(st.passageiros_esperando == 0 && st.quantidade_assentos_livres == 0 &&
	      st.passageiros_embarcando == 0,
	      "estado final inconsistente: esperando=%d livres=%d embarcando=%d",
	      st.passageiros_esperando, st.quantidade_assentos_livres,
	      st.passageiros_embarcando);
	pthread_mutex_unlock(&st.lock);
 
	printf("    info: %d carros\n", carros);
	return falhas;
}
 
/* ------------------------------------------------------------------ */
/* execucao: cada cenario em um processo filho                         */
/* ------------------------------------------------------------------ */
 
static int executar(const char *nome, int (*f)(const void *), const void *arg)
{
	int status = 0;
	pid_t pid;
 
	printf("==> %s\n", nome);
	fflush(stdout);
 
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid == 0) {
		int r;
 
		signal(SIGALRM, on_alarm);
		r = f(arg);
		fflush(stdout);
		_exit(r ? 1 : 0);
	}
 
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		printf("    OK\n");
		return 0;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
		printf("    TRAVOU: timeout (provavel deadlock ou aviso perdido)\n");
	else if (WIFSIGNALED(status))
		printf("    CRASH: sinal %d\n", WTERMSIG(status));
	else
		printf("    FALHOU\n");
	return 1;
}
 
int main(int argc, char **argv)
{
	int rodadas = argc > 1 ? atoi(argv[1]) : 3;
	int total = 0;
	char nome[128];
 
	setvbuf(stdout, NULL, _IOLBF, 0);
 
	struct caso casos[] = {
		{"sem passageiros, carro com 0 assentos", 0, 0, 0},
		{"sem passageiros, carro com 10 assentos", 0, 10, 0},
		{"10 passageiros esperando, carro com 0 assentos", 10, 0, 0},
		{"1 passageiro, 1 assento", 1, 1, 0},
		{"1 passageiro, 50 assentos (sobram muitos)", 1, 50, 0},
		{"5 passageiros, 20 assentos (sobram assentos)", 5, 20, 0},
		{"20 passageiros, 5 assentos (sobra gente)", 20, 5, 0},
		{"20 passageiros, 1 assento", 20, 1, 0},
		{"10 passageiros, 10 assentos (exato)", 10, 10, 0},
		{"200 passageiros, 50 assentos", 200, 50, 0},
		{"200 passageiros, 500 assentos", 200, 500, 0},
		{"20 passageiros, 5 assentos, on_board lento", 20, 5, 20000},
		{"5 passageiros, 20 assentos, on_board lento", 5, 20, 20000},
		{"10 passageiros, 10 assentos, on_board lento", 10, 10, 20000},
	};
	struct cfg_estresse cfgs[] = {
		{300, 10, 0, 1},
		{300, 10, 200, 2},
		{500, 100, 0, 3},
		{500, 100, 100, 4},
		{600, 3, 50, 5},
		{400, 1, 0, 6},
		{500, 40, 0, 7},
	};
 
	printf("##### CASOS DE BORDA #####\n");
	for (size_t i = 0; i < sizeof(casos) / sizeof(casos[0]); i++)
		total += executar(casos[i].nome, cenario_basico, &casos[i]);
 
	printf("\n##### CHEGADA TARDIA #####\n");
	total += executar("passageiros chegam com o carro na estacao",
			  cenario_chegada_tardia, NULL);
 
	printf("\n##### ESTRESSE (%d rodada(s) por configuracao) #####\n", rodadas);
	for (int r = 0; r < rodadas; r++) {
		for (size_t i = 0; i < sizeof(cfgs) / sizeof(cfgs[0]); i++) {
			struct cfg_estresse c = cfgs[i];
 
			c.semente += (unsigned)r * 1000u;
			snprintf(nome, sizeof(nome),
				 "estresse: %d passageiros, ate %d assentos, on_board ate %u us (rodada %d)",
				 c.passageiros, c.max_assentos, c.atraso_on_board_us, r + 1);
			total += executar(nome, cenario_estresse, &c);
		}
	}
 
	printf("\n===== RESUMO: %s (%d cenario(s) com problema) =====\n",
	       total ? "FALHOU" : "TUDO OK", total);
	return total ? 1 : 0;
}