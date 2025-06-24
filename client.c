#include "operations.h"
#include <dirent.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DADOS 0x5
#define FIM_ARQUIVO 0x9
#define MAX_DADOS 127

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <interface>\nExemplo: sudo %s enp7s0f0\n", argv[0], argv[0]);
        return 1;
    }

    char *interface = argv[1];

    int Soquete;

    Soquete = ConexaoRawSocket(interface);
    if (Soquete == -1)
    {
        perror("Erro no soquete");
        exit(1);
    }

    char Tipo;
    Mensagem resp, req;
    char acao;
    char nomeArquivo[128];

    int seq = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    while (1)
    {
        printf("w:Cima  s:Baixo  a:Esquerda  d:Direita  k:Pedir arquivo:\n");
        scanf(" %c", &acao); // Atenção com espaço antes do %c para ignorar \n anterior, sem isso o programa pode ser que trave a execucao no meio, esperando algum input

        int movimento = 1;
        Tipo = -1;

        switch (acao)
        {
        case 'w':
            Tipo = DESLOCA_CIMA;
            break;
        case 's':
            Tipo = DESLOCA_BAIXO;
            break;
        case 'a':
            Tipo = DESLOCA_ESQUERDA;
            break;
        case 'd':
            Tipo = DESLOCA_DIREITA;
            break;
        case 'k':
            movimento = 0; // Sinaliza que 'k' será tratado em outro momento
            break;
        default:
            continue; // nao faz nada se o usuario errar
        }

        if (movimento)
        {
            resp = criaMensagem("", Tipo, seq, 0);
            int recebeu = 0;
            time_t inicio = time(NULL);

            while (!recebeu)
            {
                enviaMensagem(Soquete, resp); // envia inicialmente

                do
                {
                    // espera a resposta do servidor para tratar depois
                    recebeu = aguardaResposta(Soquete, &req, seq);
                } while (!recebeu && difftime(time(NULL), inicio) < 4);

                if (!recebeu)
                {
                    printf("Timeout. Reenviando...\n");
                    inicio = time(NULL); // reset do timeout
                }
            }

            // if que vai tratar (de maneira igual) cada caso de arquivo
            if ((req.Tipo == OK_ACK || req.Tipo == TEXTO_ACK_NOME ||
                 req.Tipo == IMAGEM_ACK_NOME || req.Tipo == VIDEO_ACK_NOME) &&
                req.Sequencia == seq)
            {
                seq = (seq + 1) % 32;

                // aqui eh so se for um OK_ACK mesmo, so printa o tabuleiro recebido no campo de dados do OK_ACK
                if (req.Tipo == OK_ACK)
                {
                    imprimeTabuleiro(req.Dados);
                }
            }

            // aqui eh se o servidor enviou o nome do arquivo, entao eh quando o usuario achou tesouro
            if (req.Tipo == TEXTO_ACK_NOME || req.Tipo == IMAGEM_ACK_NOME || req.Tipo == VIDEO_ACK_NOME)
            {
                char nomeArquivo[128];
                memcpy(nomeArquivo, req.Dados, req.Tamanho);
                nomeArquivo[req.Tamanho] = '\0';
                printf("Solicitando transferência de: %s\n", nomeArquivo);

                resp = criaMensagem(nomeArquivo, REQ_ARQ, 0, strlen(nomeArquivo));
                enviaMensagem(Soquete, resp);

                seq = (seq + 1) % 32;

                if (!recebeArquivo(Soquete, nomeArquivo))
                {
                    printf("Erro ao receber o arquivo.\n");
                    continue;
                }

                abre_com_programa_correto(".", nomeArquivo); // so usa o xdg-open pra tudo
                requisitaEstadoTabuleiro(Soquete, &seq);     // aqui eh usado o tipo proprio pra atualizar o tabuleiro depois de tudo
            }

            // Trata mensagem de erro recebida
            if (req.Tipo == ERRO)
            {
                char erroMsg[256] = {0};
                memcpy(erroMsg, req.Dados, req.Tamanho);
                erroMsg[req.Tamanho] = '\0';
                printf("ERRO do servidor: %s\n", erroMsg);
                continue; // volta para esperar novo comando do usuário
            }

            continue;
        }
        // funcao que serve caso o cliente queira pedir diretamente o arquivo, sem jogar o jogo, todo o programa gira em torno disso
        if (acao == 'k')
        {
            printf("Nome do arquivo a solicitar: ");
            fgets(nomeArquivo, sizeof(nomeArquivo), stdin);
            if (nomeArquivo[0] == '\n')
                fgets(nomeArquivo, sizeof(nomeArquivo), stdin);
            nomeArquivo[strcspn(nomeArquivo, "\n")] = '\0';

            resp = criaMensagem(nomeArquivo, REQ_ARQ, 0, strlen(nomeArquivo));
            enviaMensagem(Soquete, resp);

            seq = (seq + 1) % 32;

            FILE *f = fopen(nomeArquivo, "wb");
            if (!f)
            {
                perror("Erro ao criar arquivo local");
                continue;
            }

            int lastSeq = -1;
            while (1)
            {
                int status = recebeMensagem(Soquete, &req);
                if (status == -1)
                {
                    printf("Timeout aguardando resposta. Reenviando comando...\n");
                    break;
                }
                if (req.MarcadorInicio != INICIO)
                    continue;

                if (req.Tipo == FIM_ARQUIVO)
                {
                    Mensagem ackFim = criaMensagem("", ACK, req.Sequencia, 0);
                    enviaMensagem(Soquete, ackFim);
                    printf("Transferência concluída.\n");
                    break;
                }
                if (req.Tipo == DADOS && req.Sequencia != lastSeq)
                {
                    fwrite(req.Dados, 1, req.Tamanho, f);
                    lastSeq = req.Sequencia;
                }
                Mensagem ack = criaMensagem("", ACK, req.Sequencia, 0);
                enviaMensagem(Soquete, ack);
            }
            fclose(f);
        }
    }

    return 0;
}
