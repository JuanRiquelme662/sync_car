#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "sync_car.h"

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
        //acorda o carro em station_load_car
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
        //nao e busy wait porque a thread vai de fato dormir
        // e quando acordar vai entrar no while para checar se ainda existem assentos livres, se nao tiver, volta a dormir
        pthread_cond_wait(&station->carro_chegou, &station->lock);
    }
    //reserva o assento
    station->quantidade_assentos_livres--;
    station->passageiros_embarcando++;
    station->passageiros_esperando--;

    pthread_mutex_unlock(&station->lock);
}

//quando um carro chega a estacao essa funcao e chamada
void station_load_car(struct station *station, int count){

    pthread_mutex_lock(&station->lock);

    //determina a quantidade de assentos que poderao ser ocupados
    station->quantidade_assentos_livres = count;
    //abre espaco para as threads concorrerem pelos lugares livres
    pthread_cond_broadcast(&station->carro_chegou);
    while(station->quantidade_assentos_livres > 0 && station->passageiros_esperando > 0){
        //o carro espera que todos assentos sejam preenchidos
        //ou caso nao haja a quantidade suficiente de passageiros, que todos embarquem
        pthread_cond_wait(&station->espera_embarque, &station->lock);
    }
    //caso o carro saia com assentos livres, ele zera a variavel
    //para evitar que outros passageieros reservem assentos que nao existem
    station->quantidade_assentos_livres = 0;
    
    pthread_mutex_unlock(&station->lock);
}


