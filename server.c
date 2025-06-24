#include "operations.h"
#include <dirent.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h> // se já não estiver
#include <sys/stat.h>
#include <time.h>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <interface>\nExemplo: sudo %s enp7s0f0\n", argv[0], argv[0]);
        return 1;
    }

    char *interface = argv[1];

    int soquete = ConexaoRawSocket(interface);
    if (soquete < 0)
    {
        perror("Erro ao abrir raw socket");
        return 1;
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    int px = 0, py = 7;
    Mensagem req, resp;
    unsigned char mapa[BOARD_SIZE * BOARD_SIZE];

    Posicao tesouros[NUM_TESOUROS];
    Posicao tesourosEncontrados[NUM_TESOUROS];
    int numTesourosEncontrados = 0;
    Posicao posicoesVisitadas[BOARD_SIZE * BOARD_SIZE];
    int numPosicoesVisitadas = 0;

    // esse trecho serve so pra marcar a casa inicial como ja percorrida, ja que
    // se o jogador incia nessa casa ela ja ta percorrida
    posicoesVisitadas[0].x = 0;
    posicoesVisitadas[0].y = 7;
    numPosicoesVisitadas = 1;

    unsigned char seq = 0; // sequência do servidor para envios

    inicializaTesouros(tesouros);
    while (1)
    {
        // 1) Recebe mensagem
        int status = recebeMensagem(soquete, &req);
        if (status == -1)
            continue;

        if (req.MarcadorInicio != INICIO)
            continue;
        switch (req.Tipo)
        {

        case REQ_ARQ:
            // 1) Extrai o nome do arquivo solicitado
            char nome[128];
            memcpy(nome, req.Dados, req.Tamanho);
            nome[req.Tamanho] = '\0';
            printf("Cliente requisitou arquivo: %s\n", nome);

            // 2) Monta o caminho em tesouros/
            char path[256];
            snprintf(path, sizeof(path), "tesouros/%s", nome);

            // 3) checa se o arquivo de fato existe
            struct stat st;
            if (stat(path, &st) < 0)
            {
                perror("Arquivo não encontrado em tesouros/");
                // nao faz nada quando entra aqui, mas o servidor ainda marca como tesouro encontrado, o jogo so segue
                break;
            }

            // 4) envia com base no nome montado
            enviaArquivoPorNome(soquete, path, &seq);
            break;

        case DESLOCA_BAIXO:
        {
            processaMovimento(&px, &py, 0, 1, posicoesVisitadas, &numPosicoesVisitadas, mapa,
                              tesourosEncontrados, &numTesourosEncontrados, tesouros, soquete, req);
            break;
        }
        case DESLOCA_CIMA:
        {
            processaMovimento(&px, &py, 0, -1, posicoesVisitadas, &numPosicoesVisitadas, mapa,
                              tesourosEncontrados, &numTesourosEncontrados, tesouros, soquete, req);
            break;
        }
        case DESLOCA_ESQUERDA:
        {
            processaMovimento(&px, &py, -1, 0, posicoesVisitadas, &numPosicoesVisitadas, mapa,
                              tesourosEncontrados, &numTesourosEncontrados, tesouros, soquete, req);
            break;
        }
        case DESLOCA_DIREITA:
        {
            processaMovimento(&px, &py, 1, 0, posicoesVisitadas, &numPosicoesVisitadas, mapa,
                              tesourosEncontrados, &numTesourosEncontrados, tesouros, soquete, req);
            break;
        }
        // tipo que a gente criou so pra enviar o estado do tabuleiro, como o servidor envia toda vez o tabuleiro inteiro, acabou precisando desse tipo
        case ESTADO_TABULEIRO:
            atualizaMapa(mapa, px, py, tesourosEncontrados, numTesourosEncontrados, posicoesVisitadas, numPosicoesVisitadas);
            resp = criaMensagem((char *)mapa, OK_ACK, req.Sequencia, BOARD_SIZE * BOARD_SIZE);
            enviaMensagem(soquete, resp);
            break;
        default:
            // aqui eh so se deu alguma coisa muito errada, ele retorna erro
            resp = criaMensagem("", ERRO, req.Sequencia, 0);
            enviaMensagem(soquete, resp);
            break;
        }
    }
    return 0;
}
