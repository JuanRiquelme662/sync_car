#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

//strutct que representa a stacao
struct station {
    //mutex que trava o acesso as variaveis, impedindo que duas threads acessem ao mesmo tempo
    pthread_mutex_t lock;
    //onde os passageiros esperam o carro chegar
    pthread_cond_t carro_chegou;
    //onde o carro espera o fim do embarque
    pthread_cond_t espera_embarque;
    int passageiros_esperando;
    int quantidade_assentos_livres;
    //guarda a quantidade de passageiros que estao embarcando e quando chega a zero significa que todos liberados embarcaram
    int passageiros_embarcando;
    //alem dessa chacagem, ele tambem checa se ainda existem assentos livres, se houver assentos livres, libera mais passageiros,
    //se ainda houver assentos livres mas nao tiver mais passageiros, libera o carro
};

//responsavel por iniciar os campos de station
void station_init(struct station *station){
    //inicializacao do mutex
    pthread_mutex_init(&station->lock, NULL);
    //inicializacao das variaves de condicao
    pthread_cond_init(&station->carro_chegou, NULL);
    pthread_cond_init(&station->espera_embarque, NULL);
    //inicializacao dos contadores
    station->passageiros_esperando = 0;
    station->quantidade_assentos_livres = 0;
    station->passageiros_embarcando = 0;
}

//funcao chamada quando um passageiro entra no carro
void station_on_board(struct station *station){
    //pega a "chave" do mutex, bloquadno a entreada de outras threads
    pthread_mutex_lock(&station->lock);
    station->passageiros_embarcando--;
    if(station->passageiros_embarcando == 0){
        //se nao houver mais passageiros embarcando libera o carro para checar se ja pode ir embora
        pthread_cond_signal(&station->espera_embarque);
    }
    //solta a "chave"
    pthread_mutex_unlock(&station->lock);
}

//funcao chamada quando um passageiro chega a estacao e quer entrar no carro
void station_wait_for_car(struct station *station){
    pthread_mutex_lock(&station->lock);
    station->passageiros_esperando++;
    //espera existir um assento livre para reservar
    while(station->quantidade_assentos_livres == 0){
        //a thread vai ser acordada em station_load_car
        pthread_cond_wait(&station->carro_chegou, &station->lock);
    }
    //reserva o assento
    station->quantidade_assentos_livres--;
    station->passageiros_embarcando++;
    station->passageiros_esperando--;
    pthread_mutex_unlock(&station->lock);
}

//quando um carro chega a estacao essa funcao e chamada
//void station_load_car(struct station *station, int count){}


