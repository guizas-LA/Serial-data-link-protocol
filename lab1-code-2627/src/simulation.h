// RCOM 2026/2027
//
// Error and delay simulation, used to characterise the protocol efficiency
// (slide 31 of the guide). Everything is applied at the receiver.

#ifndef SIMULATION_H
#define SIMULATION_H

////////////////////////////////////////////////
// PARA SIMULAR, MUDAR AQUI (0 = simulação desligada)
////////////////////////////////////////////////

// Probabilidade de 0.0 a 1.0 de simular um erro no cabeçalho de cada trama 
// recebida sem erros. A trama é ignorada, o emissor faz timeout e retransmite.
#define FER_HEADER 0.0

// Probabilidade de 0.0 a 1.0 de simular um erro no campo de dados de cada trama 
// recebida sem erros. A trama é descartada e o recetor responde com REJ.
#define FER_DATA 0.0

// Atraso de propagação simulado em milissegundos aplicado a cada trama recebida.
#define T_PROP_MS 0


void initSimulation();

void simulateDelay();

int simulateHeaderError();

int simulateDataError();

#endif // SIMULATION_H
