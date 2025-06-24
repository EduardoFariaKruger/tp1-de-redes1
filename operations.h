#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

#ifndef __OPERATIONS__
#define __OPERATIONS__

#define BUFFER 127
#define DEVICE "enp4s0"
#define MAX_DADOS 127
#define BOARD_SIZE 8

#define INICIO 0x7e

// tipos de mensagens
#define ACK 0x0
#define NACK 0x1
#define OK_ACK 0x2
#define REQ_ARQ 0x3
#define TAMANHO 0x4
#define DADOS 0x5
#define TEXTO_ACK_NOME 0x6
#define VIDEO_ACK_NOME 0x7
#define IMAGEM_ACK_NOME 0x8
#define FIM_ARQUIVO 0x9
#define DESLOCA_DIREITA 0xA
#define DESLOCA_CIMA 0xB
#define DESLOCA_BAIXO 0xC
#define DESLOCA_ESQUERDA 0xD
#define ESTADO_TABULEIRO 0xE
#define ERRO 0xF

#define NUM_TESOUROS 8

typedef struct
{
    int x, y;
} Posicao;

typedef struct Mensagem
{
    unsigned char MarcadorInicio;   // Marca o inicio da mensagem
    unsigned char Tamanho : 7;      // Define o tamanho da mensagem
    unsigned char Sequencia : 5;    // Define a ordem de sequencia da mensagem
    unsigned char Tipo : 4;         // O tipo da mensagem (ACK, NACKS, etc...)
    unsigned char checksum;         // Verifica o checksum da mensagem sobre os campos tamanho, sequencia, tipo e dados
    unsigned char Dados[MAX_DADOS]; // Os dados que serao enviados da mensagem
} Mensagem;

Mensagem criaMensagem(const void *Dados, unsigned char Tipo, int Sequencia, unsigned char tamanho);

int cmpmsg(Mensagem priMsg, Mensagem ultMsg);

void enviaMensagem(int soquete, Mensagem m);

int recebeMensagem(int soquete, Mensagem *msg);

int ConexaoRawSocket(char *device);

void enviaArquivoPorNome(int soquete, const char *path, unsigned char *seqPtr);

void desenhaTabuleiro(unsigned char *mapa);

unsigned char calculaChecksum(Mensagem *m);

void debugPrintMensagem(const char *prefix, Mensagem *m);

void imprimeTabuleiro(const unsigned char *mapa);

void inicializaTesouros(Posicao tesouros[]);

int encontrouTesouro(Posicao tesouros[], int x, int y, int *id,
                     Posicao tesourosEncontrados[], int *numTesourosEncontrados);

int ConexaoRawSocket(char *device);

void enviaAck(int soquete, int seq);

void enviaNack(int soquete, int seq);

void enviaOkAck(int soquete, int seq, const char *dados, int tamanho);

int aguardaResposta(int soquete, Mensagem *req, int seqEsperado);

int recebeArquivo(int soquete, const char *nomeArquivo);

void requisitaEstadoTabuleiro(int soquete, int *seq);

void abre_com_programa_correto(const char *dir, const char *filename);

void atualizaMapa(unsigned char mapa[], int px, int py,
                  const Posicao tesourosEncontrados[], int numTesourosEncontrados,
                  const Posicao posicoesVisitadas[], int numPosicoesVisitadas);

void adicionaPosicaoVisitada(Posicao posicoesVisitadas[], int x, int y, int *numPosicoesVisitadas);

int tem_espaco_disponivel(const char *path, unsigned long tamanho_arquivo);

void processaMovimento(int *px, int *py, int deltaX, int deltaY, Posicao *posicoesVisitadas, int *numPosicoesVisitadas,
                       unsigned char *mapa, Posicao *tesourosEncontrados, int *numTesourosEncontrados,
                       Posicao *tesouros, int soquete, Mensagem req);

int tesouroJaEncontrado(Posicao tesourosEncontrados[], int numTesourosEncontrados, int x, int y);

int posicaoJaVisitada(Posicao posicoesVisitadas[], int numPosicoesVisitadas, int x, int y);

#endif
