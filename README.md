# Sincronização de Metrô (METROREC)

Repositório desenvolvido para a implementação de um sistema de concorrência e sincronização de *threads* em C, estruturado para automatizar o embarque seguro de passageiros nos vagões da METROREC.

## Sobre o Projeto

O projeto resolve o problema clássico de sincronização estilo *Caltrain*, utilizando estritamente a biblioteca POSIX Threads (`pthreads`). O design da solução cumpre restrições rigorosas impostas pelo enunciado:

* **Uso restrito:** Apenas um único *lock* (mutex) e variáveis de condição por estação.

* **Proibições:** Ausência total de semáforos e de espera ativa (*busy-wait*).

* **Concorrência:** Garantia de embarque simultâneo e ordenado de múltiplos passageiros.

## Como Compilar e Executar

1. **Clonar o repositório:**

   ```
   git clone https://github.com/JuanRiquelme662/sync_car.git
   cd sync_car
   
   ```

2. **Compilar o projeto:**
   O `Makefile` está configurado para gerar a build de forma limpa (separando a compilação em objetos `.o` para isolar *warnings* do código de teste):

   ```
   make clean
   make
   
   ```

3. **Executar a aplicação/testes:**

   ```
   ./sync_car
   
   ```

## Estrutura de Arquivos

* `sync_car.c` — Implementação principal da `struct station` e das funções de controle (`station_init`, `station_load_car`, `station_wait_for_car`, `station_on_board`).

* `sync_car.h` — Cabeçalho com as definições da estrutura e protótipos.

* `sync_car-runner.c` — Módulo de testes oficial fornecido pelo professor para estresse e validação da concorrência.

* `Makefile` — Automação da compilação com separação de arquivos objeto e regras rigorosas de *warnings* (`-Wall -Wextra`).

* `jrxs.pdf` — Documentação técnica detalhada, contendo o diário de tentativas, arquitetura, decisões técnicas e evidências dos testes.

* `.gitignore` — Arquivo de configuração para ignorar binários e lixo de compilação.

## Ambiente de Desenvolvimento

O projeto foi desenvolvido por **Juan Riquelme Xavier de Souza**. e validado no seguinte ambiente:

* **Sistema Operacional:** Ubuntu 26.04 LTS (via WSL no Windows)

* **Compilador:** GCC versão 15.2.0

* **Ferramenta de Build:** GNU Make

* **Validação Extra:** Testes executados com a flag `-fsanitize=thread` para garantir ausência de vazamentos e condições de corrida (*race conditions*).
