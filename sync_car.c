#include <pthread.h>
#include <stdio.h>

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

//responsavel por iniciar o objeto station
station_init(struct station *station){

}

//quando um carro chega a estacao essa funcao e chamada
station_load_car(struct station *station, int count){

}

//funcao chamada quando um passageiro chega a estacao e quer entrar no carro
station_wait_for_car(struct station *station){

}

//funcao chamada quando um passageiro entra no carro
station_on_board(struct station *station){

}