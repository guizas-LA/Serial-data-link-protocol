PROTOCOLO DE LIGAÇÃO DE DADOS - 1.º TRABALHO LABORATORIAL
Redes de Computadores (RCOM) - FEUP - 2026/2027
==========================================================

Autores
-------
- Guilherme Silva
- Tomás Silva


Objetivo do projeto
-------------------

O objetivo do trabalho é implementar um protocolo de ligação de dados que permita
transferir um ficheiro de forma fiável entre dois computadores ligados por uma porta
série RS-232, seguindo a especificação do guião.

O protocolo usa a variante Stop & Wait (janela unitária e numeração módulo 2) e
implementa:
- estabelecimento e terminação da ligação com tramas de supervisão (SET/UA e DISC/DISC/UA);
- tramas de informação (I) delimitadas por flags, com byte stuffing para garantir transparência;
- deteção de erros no cabeçalho (BCC1) e no campo de dados (BCC2);
- confirmações positivas (RR) e negativas (REJ);
- retransmissão por timeout, com um número máximo de tentativas configurável;
- deteção e descarte de tramas duplicadas;
- estatísticas da ligação, incluindo o débito e a eficiência.

Por cima do protocolo foi desenvolvida uma aplicação simples que envia o ficheiro em
pacotes de controlo (START e END, com o tamanho do ficheiro em TLV) e pacotes de dados
(com o campo L2 L1). As duas camadas são independentes: a aplicação só usa as funções
llOpenTx/llOpenRx, llSend, llReceive e llCloseTx/llCloseRx, e não conhece a estrutura
das tramas.


Descrição dos ficheiros
-----------------------

src/main.c                (fornecido, não alterado)
    Lê os argumentos da linha de comandos (porta, baudrate, papel tx/rx e ficheiro)
    e chama a camada de aplicação. Define o número de tentativas (N_TRIES = 3) e o
    timeout (TIMEOUT = 4 s).

src/application_layer.c/.h
    Camada de aplicação. No emissor lê o ficheiro, envia o pacote START, os pacotes
    DATA com os fragmentos do ficheiro e o pacote END. No recetor recebe os pacotes,
    escreve o ficheiro e verifica se o tamanho recebido coincide com o anunciado no START.

src/link_layer.c/.h       (o .h é fornecido e não foi alterado)
    Camada de ligação de dados: implementa a API usada pela aplicação.
    - llOpenTx / llOpenRx: abrem a porta série e trocam SET/UA.
    - llSend: monta a trama I e envia-a até receber o RR, retransmitindo após um
      timeout ou um REJ.
    - llReceive: recebe tramas I e aplica as regras de receção do guião (trama nova:
      entrega e RR; duplicado: descarta e RR; erro nos dados: descarta e REJ).
    - llCloseTx / llCloseRx: trocam DISC/DISC/UA, fecham a porta e mostram as estatísticas.
    A função transmit() concentra num único ciclo toda a lógica de retransmissão
    (usada pelas tramas I, SET e DISC).

src/frame.c/.h
    Tudo o que diz respeito às tramas: constantes (FLAG, A, C, ESC), construção das
    tramas I e de supervisão, byte stuffing, cálculo do BCC2, escrita na porta série
    e a máquina de estados que lê e valida uma trama byte a byte (readFrame), fazendo
    o destuffing e a verificação de BCC1 e BCC2.

src/alarm.c/.h
    Alarme usado nos timeouts (SIGALRM). Permite ligar e desligar o alarme e conta
    quantos timeouts ocorreram.

src/statistics.c/.h
    Estatísticas da ligação: tramas enviadas e recebidas, bytes de dados,
    retransmissões, REJ, duplicados, timeouts e tempo total. Calcula também o débito
    útil R (bit/s) e a eficiência S = R / C, em que C é o baudrate.

src/simulation.c/.h
    Simulação de erros e de atraso de propagação no recetor, para a caracterização
    da eficiência do protocolo. Por omissão está desligada (todos os valores a 0).
    Para a ativar, alteram-se os valores no topo de simulation.h e volta-se a compilar:
        FER_HEADER  probabilidade de erro no cabeçalho de cada trama I
        FER_DATA    probabilidade de erro no campo de dados de cada trama I
        T_PROP_MS   atraso de propagação simulado, em milissegundos

src/serial_port.c/.h      (fornecidos, não alterados)
    Biblioteca de acesso à porta série.

cable/cable.c             (fornecido, não alterado)
    Cabo virtual que liga /tmp/ttyS10 a /tmp/ttyS11 e permite desligar a ligação ou
    adicionar ruído durante os testes.

Makefile                  (fornecido, não alterado)
    Compila o programa e o cabo virtual e tem atalhos para correr o emissor e o recetor.

penguin.gif
    Ficheiro de exemplo usado nos testes.


Como compilar e executar
------------------------

1. Compilar:
       $ make

2. Arrancar o cabo virtual (requer o socat instalado):
       $ make run_cable

3. Noutro terminal, arrancar o recetor:
       $ make run_rx
   (ou: ./bin/main /tmp/ttyS11 9600 rx penguin-received.gif)

4. Noutro terminal, arrancar o emissor:
       $ make run_tx
   (ou: ./bin/main /tmp/ttyS10 9600 tx penguin.gif)

5. Verificar que o ficheiro recebido é igual ao enviado:
       $ make check_files

Para testar a robustez, durante a transferência pode usar-se a consola do cabo virtual
para desligar a ligação ou adicionar ruído.


Dificuldade encontrada no teste com ruído e como foi resolvida
--------------------------------------------------------------

Testes realizados (sempre com o penguin.gif e o cabo virtual):
1. transferência normal;
2. um timeout (o recetor arranca depois do emissor);
3. emissor sem recetor;
4. canal desligado durante 6 s;
5. ruído no canal;
6. perda de confirmações (tramas duplicadas).

Os testes 1, 2, 3, 4 e 6 passaram logo. O teste 5 só passava com ruído curto ou
fraco (por exemplo BER 3e-5 durante 2 s ou 5e-6 durante 5 s). Com ruído mais forte
(BER 1e-5 durante 5 s ou mais) surgiam dois problemas:

Problema 1 - Um REJ gastava uma tentativa de retransmissão
    Com ruído forte, uma trama de 1000 bytes chegava corrompida várias vezes seguidas.
    O recetor respondia corretamente com REJ, mas cada REJ contava como uma tentativa
    e, com N_TRIES = 3, só havia 4 envios no total. Depois de 4 REJ seguidos o emissor
    desistia, apesar de a ligação estar a funcionar.

    Solução: o limite de tentativas passou a aplicar-se apenas aos timeouts, como
    indica o guião ("in the event of a time-out, a maximum number of retransmission
    attempts must be made"). Um REJ prova que o recetor está ativo, por isso provoca
    uma retransmissão imediata sem gastar uma tentativa. Para evitar um ciclo infinito
    se o ruído nunca parar, existe um limite de segurança separado (MAX_REJ_RETRIES = 20).
    Um canal morto continua a ser detetado, porque não gera REJ mas sim timeouts.

Problema 2 - O recetor ficava bloqueado para sempre
    Quando o emissor desistia, o recetor ficava preso no llReceive. A porta série está
    configurada para leitura bloqueante e, ao contrário do emissor, o recetor nunca
    ligava o alarme, por isso nada interrompia a espera por um byte que nunca chegava.

    Solução: o recetor passou a usar um alarme de inatividade em cada leitura
    (função readFrameOrGiveUp, no llReceive e no llCloseRx). Um emissor ativo nunca
    fica calado mais do que (N_TRIES + 1) timeouts, por isso, se passarem
    TIMEOUT x (N_TRIES + 2) = 20 s sem chegar nenhuma trama, o recetor conclui que o
    emissor desistiu e termina com erro. O llOpenRx continua a esperar pelo SET sem
    limite, para que o recetor possa ser arrancado antes do emissor.

Resultado
    Depois das duas correções, os 6 testes passam e o ficheiro recebido é sempre
    igual ao original. No teste 5, o ruído que antes fazia falhar (BER 1e-5 e até
    1e-4 durante 5 s) passa com 4 a 5 REJ e nenhum timeout. Se o emissor for terminado
    a meio da transferência, o recetor termina sozinho ao fim de 20 s.
