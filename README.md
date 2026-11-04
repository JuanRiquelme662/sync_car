# Sincronização de Metrô (METROREC)

Este repositório contém a solução para o problema clássico de concorrência e sincronização de threads em C (estilo *Caltrain*), desenvolvido para automatizar o embarque de passageiros nos vagões da METROREC[cite: 2].

## Sobre o Projeto

O objetivo principal é garantir que os vagões e os passageiros (representados por *threads* independentes) interajam de forma ordenada e segura. A implementação cumpre rigorosamente as restrições da biblioteca POSIX Threads (`pthreads`), limitando-se ao uso de um único *mutex* e variáveis de condição, sem recorrer a semáforos ou a espera ativa (*busy-wait*).

## Tecnologias Utilizadas

* **Linguagem:** C (C99)
* **Concorrência:** POSIX Threads (`pthread.h`)
* **Automação de Compilação:** Make / Makefile

## Pré-requisitos

Para compilar e executar o projeto, precisas de um ambiente compatível com Unix/Linux/macOS e ferramentas essenciais instaladas (como `gcc` e `make`).

## Como Compilar e Executar

1. **Clonar o repositório:**
   ```bash
   git clone [https://github.com/teu-utilizador/nome-do-repositorio.git](https://github.com/teu-utilizador/nome-do-repositorio.git)
   cd nome-do-repositorio