/*
 * Bateria de testes extra para sync_car.c
 *
 * Cada teste cria uma estação nova, monta um cenário e confere os contadores
 * da struct e o comportamento das threads. Se algum teste travar (deadlock),
 * um alarme encerra o programa e diz qual teste estava rodando.
 *
 * Observação: o CÓDIGO DE TESTE usa usleep() em laços para esperar as threads
 * chegarem a um estado. Isso é só no teste; a implementação em sync_car.c
 * não usa nenhuma espera ativa.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>

#include "sync_car.h"

#define MAX_PASSAGEIROS 200
#define TEMPO_LIMITE    10     // segundos por teste antes de considerar deadlock
#define ESPERA_MAX_MS   3000   // quanto tempo os laços de espera aguardam

/* ---------- Estado compartilhado do TESTE (não da estação) ---------- */

static pthread_mutex_t trava_teste;     // protege as duas variáveis abaixo
static int passageiros_retornaram = 0;  // quantos saíram do station_wait_for_car
static int vagao_partiu = 0;            // 1 quando station_load_car retornou

static const char *teste_atual = "nenhum";
static int testes_ok = 0;

/* ---------- Utilidades ---------- */

static void alarme(int sinal)
{
    (void) sinal; // parâmetro obrigatório, mas não usado
    fprintf(stderr, "\n[FALHOU] %s: travou por mais de %d s (provável deadlock)\n",
            teste_atual, TEMPO_LIMITE);
    exit(EXIT_FAILURE);
}

static void verificar(int condicao, const char *mensagem)
{
    if (!condicao) {
        fprintf(stderr, "\n[FALHOU] %s: %s\n", teste_atual, mensagem);
        exit(EXIT_FAILURE);
    }
}

static void dormir_ms(int ms)
{
    usleep(ms * 1000);
}

static void comecar_teste(const char *nome)
{
    teste_atual = nome;
    pthread_mutex_lock(&trava_teste);
    passageiros_retornaram = 0;
    vagao_partiu = 0;
    pthread_mutex_unlock(&trava_teste);
    alarm(TEMPO_LIMITE);
}

static void terminar_teste(void)
{
    alarm(0);
    testes_ok++;
    printf("[OK] %s\n", teste_atual);
}

static int ler_retornaram(void)
{
    pthread_mutex_lock(&trava_teste);
    int valor = passageiros_retornaram;
    pthread_mutex_unlock(&trava_teste);
    return valor;
}

static int ler_partiu(void)
{
    pthread_mutex_lock(&trava_teste);
    int valor = vagao_partiu;
    pthread_mutex_unlock(&trava_teste);
    return valor;
}

/* Lê os três contadores da estação usando o lock dela. */
static void ler_estacao(struct station *s, int *livres, int *esperando, int *embarcando)
{
    pthread_mutex_lock(&s->lock);
    *livres = s->quantidade_assentos_livres;
    *esperando = s->passageiros_esperando;
    *embarcando = s->passageiros_embarcando;
    pthread_mutex_unlock(&s->lock);
}

/* Confere que nenhum contador ficou negativo. */
static void verificar_nao_negativos(struct station *s)
{
    int livres, esperando, embarcando;
    ler_estacao(s, &livres, &esperando, &embarcando);
    verificar(livres >= 0, "quantidade_assentos_livres ficou negativo");
    verificar(esperando >= 0, "passageiros_esperando ficou negativo");
    verificar(embarcando >= 0, "passageiros_embarcando ficou negativo");
}

/* Espera até que 'n' passageiros estejam registrados como esperando. */
static int esperar_esperando(struct station *s, int n)
{
    int livres, esperando, embarcando;
    for (int ms = 0; ms < ESPERA_MAX_MS; ms++) {
        ler_estacao(s, &livres, &esperando, &embarcando);
        if (esperando == n)
            return 1;
        dormir_ms(1);
    }
    return 0;
}

/* Espera até que pelo menos 'n' passageiros tenham saído do wait_for_car. */
static int esperar_retornaram(int n)
{
    for (int ms = 0; ms < ESPERA_MAX_MS; ms++) {
        if (ler_retornaram() >= n)
            return 1;
        dormir_ms(1);
    }
    return 0;
}

/* Espera até que o vagão (rodando em outra thread) tenha partido. */
static int esperar_partida(void)
{
    for (int ms = 0; ms < ESPERA_MAX_MS; ms++) {
        if (ler_partiu())
            return 1;
        dormir_ms(1);
    }
    return 0;
}

/* ---------- Threads ---------- */

/* Passageiro que só embarca; quem chama station_on_board é o teste. */
static void *thread_passageiro(void *arg)
{
    struct station *s = (struct station *) arg;
    station_wait_for_car(s);
    pthread_mutex_lock(&trava_teste);
    passageiros_retornaram++;
    pthread_mutex_unlock(&trava_teste);
    return NULL;
}

/* Passageiro "completo": embarca e ele mesmo avisa que sentou. */
static void *thread_passageiro_completo(void *arg)
{
    struct station *s = (struct station *) arg;
    station_wait_for_car(s);
    pthread_mutex_lock(&trava_teste);
    passageiros_retornaram++;
    pthread_mutex_unlock(&trava_teste);
    station_on_board(s);
    return NULL;
}

typedef struct {
    struct station *station;
    int assentos;
} ArgsVagao;

static void *thread_vagao(void *arg)
{
    ArgsVagao *args = (ArgsVagao *) arg;
    station_load_car(args->station, args->assentos);
    pthread_mutex_lock(&trava_teste);
    vagao_partiu = 1;
    pthread_mutex_unlock(&trava_teste);
    return NULL;
}

static void criar_passageiros(struct station *s, pthread_t ids[], int n,
                              void *(*funcao)(void *))
{
    for (int i = 0; i < n; i++) {
        if (pthread_create(&ids[i], NULL, funcao, s) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
}

static void juntar_threads(pthread_t ids[], int n)
{
    for (int i = 0; i < n; i++)
        pthread_join(ids[i], NULL);
}

static pthread_t criar_vagao(ArgsVagao *args)
{
    pthread_t id;
    pthread_mutex_lock(&trava_teste);
    vagao_partiu = 0;
    pthread_mutex_unlock(&trava_teste);
    if (pthread_create(&id, NULL, thread_vagao, args) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    return id;
}

/*
 * Manda um vagão com exatamente 'n' assentos para levar os 'n' passageiros
 * que ainda estão esperando, e junta as threads. Usado para "limpar" a
 * estação no fim dos testes que deixam gente na plataforma.
 */
static void liberar_passageiros(struct station *s, pthread_t ids[], int n)
{
    int ja_retornaram = ler_retornaram();
    ArgsVagao args = {s, n};
    pthread_t vagao = criar_vagao(&args);

    verificar(esperar_retornaram(ja_retornaram + n),
              "limpeza: passageiros restantes não embarcaram");
    for (int i = 0; i < n; i++)
        station_on_board(s);
    verificar(esperar_partida(), "limpeza: vagão não partiu");

    pthread_join(vagao, NULL);
    juntar_threads(ids, n);
}

/* ---------- Testes ---------- */

static void teste_init(void)
{
    comecar_teste("T01 station_init zera os contadores");
    struct station s;
    station_init(&s);
    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "quantidade_assentos_livres deveria começar em 0");
    verificar(esperando == 0, "passageiros_esperando deveria começar em 0");
    verificar(embarcando == 0, "passageiros_embarcando deveria começar em 0");
    terminar_teste();
}

static void teste_vagao_vazio(void)
{
    comecar_teste("T02 vagão sem passageiros (0 e 10 assentos) parte na hora");
    struct station s;
    station_init(&s);
    station_load_car(&s, 0);   // se travar, o alarme acusa
    station_load_car(&s, 10);
    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "portas não foram fechadas (quantidade_assentos_livres != 0)");
    terminar_teste();
}

static void teste_count_negativo_vazio(void)
{
    comecar_teste("T03 [inválido] count negativo sem passageiros parte na hora");
    struct station s;
    station_init(&s);
    station_load_car(&s, -5);
    verificar_nao_negativos(&s);
    terminar_teste();
}

/* Vagão com 'assentos' <= 0 e 5 passageiros esperando: ninguém embarca. */
static void cenario_ninguem_embarca(const char *nome, int assentos)
{
    comecar_teste(nome);
    struct station s;
    station_init(&s);
    pthread_t ids[5];
    criar_passageiros(&s, ids, 5, thread_passageiro);
    verificar(esperar_esperando(&s, 5), "passageiros não chegaram na plataforma");

    station_load_car(&s, assentos);  // tem que retornar na hora
    dormir_ms(50);

    verificar(ler_retornaram() == 0, "passageiro embarcou num vagão sem assentos");
    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "quantidade_assentos_livres deveria ser 0");
    verificar(esperando == 5, "os 5 passageiros deveriam continuar esperando");
    verificar(embarcando == 0, "ninguém deveria estar embarcando");

    liberar_passageiros(&s, ids, 5);
    terminar_teste();
}

static void teste_embarque_simultaneo(void)
{
    comecar_teste("T06 [req. 5] vários passageiros saem do wait antes de qualquer on_board");
    struct station s;
    station_init(&s);
    pthread_t ids[10];
    criar_passageiros(&s, ids, 10, thread_passageiro);
    verificar(esperar_esperando(&s, 10), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 10};
    pthread_t vagao = criar_vagao(&args);

    // Nenhum on_board foi chamado ainda: os 10 precisam conseguir sair do wait.
    verificar(esperar_retornaram(10),
              "nem todos saíram do wait_for_car sem on_board (embarque não é simultâneo)");
    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(embarcando == 10, "deveria haver 10 passageiros embarcando ao mesmo tempo");
    verificar(!ler_partiu(), "vagão partiu antes dos passageiros sentarem");

    for (int i = 0; i < 10; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu depois de todos sentarem");

    pthread_join(vagao, NULL);
    juntar_threads(ids, 10);
    terminar_teste();
}

static void teste_nao_parte_antes_de_sentar(void)
{
    comecar_teste("T07 vagão espera o ÚLTIMO on_board para partir");
    struct station s;
    station_init(&s);
    pthread_t ids[3];
    criar_passageiros(&s, ids, 3, thread_passageiro);
    verificar(esperar_esperando(&s, 3), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 3};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(3), "passageiros não embarcaram");

    station_on_board(&s);
    station_on_board(&s);
    dormir_ms(100);
    verificar(!ler_partiu(), "vagão partiu com um passageiro ainda embarcando");

    station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu depois do último on_board");

    pthread_join(vagao, NULL);
    juntar_threads(ids, 3);
    terminar_teste();
}

static void teste_mais_passageiros_que_assentos(void)
{
    comecar_teste("T08 8 passageiros, 3 assentos: embarcam exatamente 3");
    struct station s;
    station_init(&s);
    pthread_t ids[8];
    criar_passageiros(&s, ids, 8, thread_passageiro);
    verificar(esperar_esperando(&s, 8), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 3};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(3), "os 3 passageiros não embarcaram");
    dormir_ms(100);
    verificar(ler_retornaram() == 3, "embarcou mais gente do que o número de assentos");

    for (int i = 0; i < 3; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão cheio não partiu");
    pthread_join(vagao, NULL);

    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "portas não foram fechadas");
    verificar(esperando == 5, "os 5 que sobraram deveriam continuar esperando");
    verificar(embarcando == 0, "ninguém deveria estar embarcando");

    dormir_ms(50);
    verificar(ler_retornaram() == 3, "passageiro embarcou depois do vagão partir");

    // Os 5 primeiros ids não são necessariamente os que embarcaram, então
    // libera os 5 restantes e junta todas as 8 threads no final.
    int ja_retornaram = ler_retornaram();
    ArgsVagao args2 = {&s, 5};
    pthread_t vagao2 = criar_vagao(&args2);
    verificar(esperar_retornaram(ja_retornaram + 5), "segundo vagão não levou os 5");
    for (int i = 0; i < 5; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "segundo vagão não partiu");
    pthread_join(vagao2, NULL);
    juntar_threads(ids, 8);
    terminar_teste();
}

static void teste_menos_passageiros_que_assentos(void)
{
    comecar_teste("T09 2 passageiros, 5 assentos: parte quando a plataforma esvazia");
    struct station s;
    station_init(&s);
    pthread_t ids[2];
    criar_passageiros(&s, ids, 2, thread_passageiro);
    verificar(esperar_esperando(&s, 2), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 5};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(2), "passageiros não embarcaram");
    station_on_board(&s);
    station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu com a plataforma vazia");
    pthread_join(vagao, NULL);

    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "vagão partiu com quantidade_assentos_livres != 0 (portas abertas)");
    juntar_threads(ids, 2);
    terminar_teste();
}

static void teste_passageiro_atrasado(void)
{
    comecar_teste("T10 passageiro que chega depois do vagão partir não embarca");
    struct station s;
    station_init(&s);

    // Vagão de 5 assentos chega e parte vazio (sobram 5 assentos).
    station_load_car(&s, 5);

    pthread_t ids[1];
    criar_passageiros(&s, ids, 1, thread_passageiro);
    verificar(esperar_esperando(&s, 1), "passageiro não chegou na plataforma");
    dormir_ms(100);
    verificar(ler_retornaram() == 0, "passageiro embarcou num vagão que já tinha partido");

    liberar_passageiros(&s, ids, 1);
    terminar_teste();
}

static void teste_chega_durante_embarque(void)
{
    comecar_teste("T11 passageiro que chega durante o embarque entra no mesmo vagão");
    struct station s;
    station_init(&s);
    pthread_t ids[2];
    criar_passageiros(&s, ids, 1, thread_passageiro);
    verificar(esperar_esperando(&s, 1), "primeiro passageiro não chegou");

    ArgsVagao args = {&s, 3};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(1), "primeiro passageiro não embarcou");

    // O vagão ainda está na estação (o primeiro não chamou on_board).
    criar_passageiros(&s, &ids[1], 1, thread_passageiro);
    verificar(esperar_retornaram(2), "passageiro que chegou durante o embarque não entrou");
    verificar(!ler_partiu(), "vagão partiu antes da hora");

    station_on_board(&s);
    station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu");
    pthread_join(vagao, NULL);
    juntar_threads(ids, 2);
    terminar_teste();
}

static void teste_sem_espera_ativa(void)
{
    comecar_teste("T12 [req. 6] threads esperando não gastam CPU (sem busy-wait)");
    struct station s;
    station_init(&s);
    pthread_t ids[20];
    criar_passageiros(&s, ids, 20, thread_passageiro);
    verificar(esperar_esperando(&s, 20), "passageiros não chegaram na plataforma");

    // 10 embarcam e o vagão fica esperando o on_board; os outros 10 dormem.
    ArgsVagao args = {&s, 10};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(10), "passageiros não embarcaram");

    // clock() mede o tempo de CPU gasto pelo processo inteiro (todas as threads).
    clock_t antes = clock();
    dormir_ms(500);
    clock_t depois = clock();
    double cpu = (double) (depois - antes) / CLOCKS_PER_SEC;
    printf("     tempo de CPU em 0,5 s de espera: %.4f s\n", cpu);
    verificar(cpu < 0.1, "threads gastaram CPU enquanto deveriam estar dormindo");

    for (int i = 0; i < 10; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu");
    pthread_join(vagao, NULL);

    // Leva os 10 que sobraram.
    int ja_retornaram = ler_retornaram();
    ArgsVagao args2 = {&s, 10};
    pthread_t vagao2 = criar_vagao(&args2);
    verificar(esperar_retornaram(ja_retornaram + 10), "segundo vagão não levou os 10");
    for (int i = 0; i < 10; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "segundo vagão não partiu");
    pthread_join(vagao2, NULL);
    juntar_threads(ids, 20);
    terminar_teste();
}

static void teste_estresse(int rodadas)
{
    comecar_teste("T13 estresse: muitos vagões aleatórios (inclusive negativos)");
    printf("     rodando %d rodadas...\n", rodadas);

    for (int r = 0; r < rodadas; r++) {
        alarm(TEMPO_LIMITE); // cada rodada ganha seu próprio tempo limite
        pthread_mutex_lock(&trava_teste);
        passageiros_retornaram = 0;
        pthread_mutex_unlock(&trava_teste);

        struct station s;
        station_init(&s);
        int total = 1 + random() % MAX_PASSAGEIROS;
        pthread_t ids[MAX_PASSAGEIROS];
        criar_passageiros(&s, ids, total, thread_passageiro_completo);

        int restantes = total;
        while (restantes > 0) {
            // Garante que todos os que faltam já estão na plataforma, para o
            // número esperado de embarques ser exato.
            verificar(esperar_esperando(&s, restantes),
                      "passageiros restantes não estão na plataforma");

            int assentos = (int) (random() % 30) - 5; // entre -5 e 24
            int esperado = assentos < 0 ? 0 : assentos;
            if (esperado > restantes)
                esperado = restantes;

            int antes = ler_retornaram();
            station_load_car(&s, assentos); // os passageiros chamam on_board sozinhos
            int embarcaram = ler_retornaram() - antes;

            if (embarcaram != esperado) {
                fprintf(stderr, "\n[FALHOU] %s: rodada %d, vagão com %d assentos e %d "
                        "esperando: embarcaram %d (esperado %d)\n",
                        teste_atual, r, assentos, restantes, embarcaram, esperado);
                exit(EXIT_FAILURE);
            }

            int livres, esperando, embarcando;
            ler_estacao(&s, &livres, &esperando, &embarcando);
            verificar(livres == 0, "vagão partiu com as portas abertas");
            verificar(embarcando == 0, "vagão partiu com passageiro embarcando");
            verificar_nao_negativos(&s);

            restantes -= embarcaram;
        }
        juntar_threads(ids, total);
    }
    terminar_teste();
}

/* ---------- Testes extras (T14 a T23) ---------- */

static void teste_assentos_igual_passageiros(void)
{
    comecar_teste("T14 5 passageiros e 5 assentos: todos embarcam e o vagão parte cheio");
    struct station s;
    station_init(&s);
    pthread_t ids[5];
    criar_passageiros(&s, ids, 5, thread_passageiro);
    verificar(esperar_esperando(&s, 5), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 5};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(5), "nem todos embarcaram");

    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0 && esperando == 0 && embarcando == 5,
              "estado errado com o vagão cheio antes dos on_board");

    for (int i = 0; i < 5; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão cheio não partiu");
    pthread_join(vagao, NULL);
    juntar_threads(ids, 5);
    terminar_teste();
}

static void teste_um_assento_por_vagao(void)
{
    comecar_teste("T15 10 esperando e 10 vagões de 1 assento: exatamente 1 por vagão");
    struct station s;
    station_init(&s);
    pthread_t ids[10];
    criar_passageiros(&s, ids, 10, thread_passageiro);
    verificar(esperar_esperando(&s, 10), "passageiros não chegaram na plataforma");

    for (int v = 0; v < 10; v++) {
        ArgsVagao args = {&s, 1};
        pthread_t vagao = criar_vagao(&args);
        verificar(esperar_retornaram(v + 1), "passageiro não embarcou no vagão de 1 assento");
        dormir_ms(20);
        verificar(ler_retornaram() == v + 1, "mais de 1 passageiro num vagão de 1 assento");
        station_on_board(&s);
        verificar(esperar_partida(), "vagão de 1 assento não partiu");
        pthread_join(vagao, NULL);
        verificar_nao_negativos(&s);
    }
    juntar_threads(ids, 10);
    terminar_teste();
}

static void teste_count_enorme(void)
{
    comecar_teste("T16 vagão com INT_MAX assentos e 3 passageiros");
    struct station s;
    station_init(&s);
    pthread_t ids[3];
    criar_passageiros(&s, ids, 3, thread_passageiro);
    verificar(esperar_esperando(&s, 3), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, INT_MAX};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(3), "passageiros não embarcaram");
    for (int i = 0; i < 3; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu com a plataforma vazia");
    pthread_join(vagao, NULL);

    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0, "portas não foram fechadas");
    juntar_threads(ids, 3);
    terminar_teste();
}

static void teste_count_int_min(void)
{
    comecar_teste("T17 [inválido] vagão com INT_MIN assentos e 4 esperando");
    struct station s;
    station_init(&s);
    pthread_t ids[4];
    criar_passageiros(&s, ids, 4, thread_passageiro);
    verificar(esperar_esperando(&s, 4), "passageiros não chegaram na plataforma");

    station_load_car(&s, INT_MIN);
    dormir_ms(50);
    verificar(ler_retornaram() == 0, "passageiro embarcou num vagão com INT_MIN assentos");
    verificar_nao_negativos(&s);

    liberar_passageiros(&s, ids, 4);
    terminar_teste();
}

static void teste_vagao_cheio_quem_chega_espera(void)
{
    comecar_teste("T18 vagão cheio: quem chega durante o embarque espera o próximo");
    struct station s;
    station_init(&s);
    pthread_t primeiros[3], atrasados[2];
    criar_passageiros(&s, primeiros, 3, thread_passageiro);
    verificar(esperar_esperando(&s, 3), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 3};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(3), "os 3 primeiros não embarcaram");

    // Vagão cheio, mas ainda na estação (ninguém chamou on_board).
    criar_passageiros(&s, atrasados, 2, thread_passageiro);
    verificar(esperar_esperando(&s, 2), "atrasados não chegaram na plataforma");
    dormir_ms(100);
    verificar(ler_retornaram() == 3, "passageiro entrou num vagão cheio");

    for (int i = 0; i < 3; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão cheio não partiu");
    pthread_join(vagao, NULL);
    juntar_threads(primeiros, 3);

    liberar_passageiros(&s, atrasados, 2);
    terminar_teste();
}

static void teste_chegadas_intercaladas(void)
{
    comecar_teste("T19 chegadas misturadas com on_board: vagão espera todos sentarem");
    struct station s;
    station_init(&s);
    pthread_t ids[4];
    criar_passageiros(&s, ids, 2, thread_passageiro);
    verificar(esperar_esperando(&s, 2), "primeiros passageiros não chegaram");

    ArgsVagao args = {&s, 4};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(2), "primeiros passageiros não embarcaram");

    station_on_board(&s);                       // 1 sentou, 1 ainda embarcando
    criar_passageiros(&s, &ids[2], 2, thread_passageiro);
    verificar(esperar_retornaram(4), "os que chegaram depois não embarcaram");

    station_on_board(&s);
    station_on_board(&s);
    dormir_ms(100);
    verificar(!ler_partiu(), "vagão partiu com um passageiro ainda embarcando");

    station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu depois de todos sentarem");
    pthread_join(vagao, NULL);
    juntar_threads(ids, 4);
    terminar_teste();
}

static void teste_duas_estacoes(void)
{
    comecar_teste("T20 duas estações ao mesmo tempo não interferem uma na outra");
    struct station a, b;
    station_init(&a);
    station_init(&b);
    pthread_t ids_a[4], ids_b[6];
    criar_passageiros(&a, ids_a, 4, thread_passageiro);
    criar_passageiros(&b, ids_b, 6, thread_passageiro);
    verificar(esperar_esperando(&a, 4), "passageiros da estação A não chegaram");
    verificar(esperar_esperando(&b, 6), "passageiros da estação B não chegaram");

    ArgsVagao args = {&a, 10};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(4), "passageiros da estação A não embarcaram");
    dormir_ms(50);
    verificar(ler_retornaram() == 4, "passageiro da estação B embarcou no vagão da A");

    int livres, esperando, embarcando;
    ler_estacao(&b, &livres, &esperando, &embarcando);
    verificar(livres == 0 && esperando == 6 && embarcando == 0,
              "o vagão da estação A alterou a estação B");

    for (int i = 0; i < 4; i++)
        station_on_board(&a);
    verificar(esperar_partida(), "vagão da estação A não partiu");
    pthread_join(vagao, NULL);
    juntar_threads(ids_a, 4);

    liberar_passageiros(&b, ids_b, 6);
    terminar_teste();
}

static void teste_embarque_simultaneo_grande(void)
{
    comecar_teste("T21 [req. 5] 150 passageiros embarcando ao mesmo tempo");
    struct station s;
    station_init(&s);
    pthread_t ids[150];
    criar_passageiros(&s, ids, 150, thread_passageiro);
    verificar(esperar_esperando(&s, 150), "passageiros não chegaram na plataforma");

    ArgsVagao args = {&s, 150};
    pthread_t vagao = criar_vagao(&args);
    verificar(esperar_retornaram(150), "nem todos saíram do wait_for_car sem on_board");

    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(embarcando == 150, "deveria haver 150 embarcando ao mesmo tempo");
    verificar(!ler_partiu(), "vagão partiu antes de todos sentarem");

    for (int i = 0; i < 150; i++)
        station_on_board(&s);
    verificar(esperar_partida(), "vagão não partiu");
    pthread_join(vagao, NULL);
    juntar_threads(ids, 150);
    terminar_teste();
}

static void teste_muitos_vagoes_sem_assento(void)
{
    comecar_teste("T22 2000 vagões sem assento (0 e negativo) com 20 esperando");
    struct station s;
    station_init(&s);
    pthread_t ids[20];
    criar_passageiros(&s, ids, 20, thread_passageiro);
    verificar(esperar_esperando(&s, 20), "passageiros não chegaram na plataforma");

    for (int v = 0; v < 2000; v++)
        station_load_car(&s, (v % 2 == 0) ? 0 : -1);

    dormir_ms(50);
    verificar(ler_retornaram() == 0, "passageiro embarcou num vagão sem assento");
    int livres, esperando, embarcando;
    ler_estacao(&s, &livres, &esperando, &embarcando);
    verificar(livres == 0 && esperando == 20 && embarcando == 0,
              "estado mudou depois de vagões sem assento");

    liberar_passageiros(&s, ids, 20);
    terminar_teste();
}

typedef struct {
    struct station *station;
    pthread_t *ids;
    int total;
} ArgsCriador;

/* Cria os passageiros aos poucos, enquanto os vagões já estão passando. */
static void *thread_criador(void *arg)
{
    ArgsCriador *args = (ArgsCriador *) arg;
    for (int i = 0; i < args->total; i++) {
        if (pthread_create(&args->ids[i], NULL, thread_passageiro_completo,
                           args->station) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
        if (random() % 4 == 0)
            usleep(random() % 500);
    }
    return NULL;
}

static void teste_estresse_chegadas_durante(int rodadas)
{
    comecar_teste("T23 estresse: passageiros chegando enquanto os vagões passam");
    printf("     rodando %d rodadas...\n", rodadas);

    for (int r = 0; r < rodadas; r++) {
        alarm(TEMPO_LIMITE);
        pthread_mutex_lock(&trava_teste);
        passageiros_retornaram = 0;
        pthread_mutex_unlock(&trava_teste);

        struct station s;
        station_init(&s);
        int total = 1 + random() % 150;
        pthread_t ids[150];
        ArgsCriador args = {&s, ids, total};
        pthread_t criador;
        if (pthread_create(&criador, NULL, thread_criador, &args) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }

        int embarcados = 0;
        while (embarcados < total) {
            int assentos = (int) (random() % 30) - 5;
            int antes = ler_retornaram();
            station_load_car(&s, assentos);
            int embarcaram = ler_retornaram() - antes;

            int maximo = assentos < 0 ? 0 : assentos;
            verificar(embarcaram <= maximo, "embarcou mais gente do que o número de assentos");

            int livres, esperando, embarcando;
            ler_estacao(&s, &livres, &esperando, &embarcando);
            verificar(livres == 0, "vagão partiu com as portas abertas");
            verificar(embarcando == 0, "vagão partiu com passageiro embarcando");
            verificar_nao_negativos(&s);

            embarcados += embarcaram;
            if (embarcaram == 0)
                dormir_ms(1); // dá tempo para mais passageiros chegarem
        }
        verificar(embarcados == total, "número total de embarques diferente do total");

        pthread_join(criador, NULL);
        juntar_threads(ids, total);
    }
    terminar_teste();
}

int main(void)
{
    pthread_mutex_init(&trava_teste, NULL);
    signal(SIGALRM, alarme);
    srandom(getpid() ^ time(NULL));

    printf("=== Testes extras do sync_car ===\n");

    teste_init();
    teste_vagao_vazio();
    teste_count_negativo_vazio();
    cenario_ninguem_embarca("T04 vagão com 0 assentos e 5 esperando: ninguém embarca", 0);
    cenario_ninguem_embarca("T05 [inválido] count negativo e 5 esperando: ninguém embarca", -3);
    teste_embarque_simultaneo();
    teste_nao_parte_antes_de_sentar();
    teste_mais_passageiros_que_assentos();
    teste_menos_passageiros_que_assentos();
    teste_passageiro_atrasado();
    teste_chega_durante_embarque();
    teste_sem_espera_ativa();
    teste_estresse(200);
    teste_assentos_igual_passageiros();
    teste_um_assento_por_vagao();
    teste_count_enorme();
    teste_count_int_min();
    teste_vagao_cheio_quem_chega_espera();
    teste_chegadas_intercaladas();
    teste_duas_estacoes();
    teste_embarque_simultaneo_grande();
    teste_muitos_vagoes_sem_assento();
    teste_estresse_chegadas_durante(100);

    printf("=== %d testes passaram ===\n", testes_ok);
    return 0;
}