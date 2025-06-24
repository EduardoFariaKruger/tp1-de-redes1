#include "operations.h"
#include <time.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

int ConexaoRawSocket(char *device)
{
    int soquete;
    struct ifreq ir;
    struct sockaddr_ll endereco;
    struct packet_mreq mr;

    soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)); /*cria socket*/
    if (soquete == -1)
    {
        printf("Erro no Socket\n");
        exit(-1);
    }

    memset(&ir, 0, sizeof(struct ifreq)); /*dispositivo eth0*/
    memcpy(ir.ifr_name, device, IFNAMSIZ);
    ir.ifr_name[IFNAMSIZ - 1] = '\0'; // Garante que termina com '\0'
    if (ioctl(soquete, SIOCGIFINDEX, &ir) == -1)
    {
        printf("Erro no ioctl\n");
        exit(-1);
    }

    memset(&endereco, 0, sizeof(endereco)); /*IP do dispositivo*/
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ir.ifr_ifindex;
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1)
    {
        printf("Erro no bind\n");
        exit(-1);
    }

    memset(&mr, 0, sizeof(mr)); /*Modo Promiscuo*/
    mr.mr_ifindex = ir.ifr_ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1)
    {
        printf("Erro ao fazer setsockopt\n");
        exit(-1);
    }

    return soquete;
}

void enviaMensagem(int soquete, Mensagem m)
{
    unsigned char *dados = (unsigned char *)&m;
    unsigned char buffer[2 * sizeof(Mensagem)];

    for (size_t i = 0; i < sizeof(Mensagem); i++)
    {
        buffer[2 * i] = dados[i];
        buffer[2 * i + 1] = 0xFF;
    }

    if (send(soquete, buffer, 2 * sizeof(Mensagem), 0) < 0)
    {
        perror("Erro no envio da mensagem!");
        exit(1);
    }
}

int recebeMensagem(int soquete, Mensagem *msg)
{
    struct timeval timeout;
    timeout.tv_sec = 4;
    timeout.tv_usec = 0;

    setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    unsigned char buffer[2 * sizeof(Mensagem)];
    int recebido = recv(soquete, buffer, 2 * sizeof(Mensagem), 0);

    if (recebido < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            return -1;
        }
        perror("Erro no recebimento");
        exit(1);
    }

    unsigned char *dados = (unsigned char *)msg;
    for (size_t i = 0; i < sizeof(Mensagem); i++)
    {
        dados[i] = buffer[2 * i];
    }

    return 1;
}

int cmpmsg(Mensagem priMsg, Mensagem ultMsg)
{
    int resultado;
    if ((priMsg.Tamanho == ultMsg.Tamanho) && (priMsg.Sequencia == ultMsg.Sequencia) && (priMsg.Tipo == ultMsg.Tipo) && !(strcmp((char *)priMsg.Dados, (char *)ultMsg.Dados)) && (priMsg.checksum == ultMsg.checksum))
        resultado = 1;
    else
        resultado = 0;

    return resultado;
}

Mensagem criaMensagem(const void *Dados, unsigned char Tipo, int Sequencia, unsigned char tamanho)
{
    Mensagem m;

    m.MarcadorInicio = INICIO;
    m.Tamanho = tamanho;
    m.Sequencia = (unsigned char)Sequencia;
    m.Tipo = (unsigned char)Tipo;

    memcpy(m.Dados, Dados, tamanho);

    m.checksum = calculaChecksum(&m);

    return m;
}

void enviaArquivoPorNome(int soquete, const char *path, unsigned char *seqPtr)
{
    struct stat st;
    if (stat(path, &st) < 0)
    {
        perror("Arquivo não encontrado");
        return;
    }

    // 1) envia pacote TAMANHO e espera ACK
    long filesize = st.st_size;
    Mensagem mT = criaMensagem((void *)&filesize, TAMANHO, *seqPtr, sizeof(filesize));

    int acknowledged = 0;
    time_t inicio;
    while (!acknowledged)
    {
        enviaMensagem(soquete, mT);

        Mensagem resp;

        int status = recebeMensagem(soquete, &resp);
        if (status == -1)
        {
            printf("Timeout aguardando resposta. Reenviando comando...\n");
            break; // Sai do loop interno para reenviar a mensagem (nesse caso ele só vai ficar repetindo o printf mesmo)
        }
        if (resp.MarcadorInicio == INICIO &&
            resp.Tipo == ACK &&
            resp.Sequencia == mT.Sequencia)
        {
            acknowledged = 1;
        }
    }
    *seqPtr = (*seqPtr + 1) % 32;

    // 2) envia DADOS em blocos e aguarda ACK/NACK (aqui que tava dando o erro VLAN)
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        perror("Erro ao abrir arquivo");
        return;
    }
    unsigned char buffer[MAX_DADOS];
    size_t lidos;
    while ((lidos = fread(buffer, 1, MAX_DADOS, f)) > 0)
    {
        Mensagem md = criaMensagem(buffer, DADOS, *seqPtr, lidos);

        int enviado = 0;

        while (!enviado)
        {
            enviaMensagem(soquete, md);
            inicio = time(NULL);

            while (!enviado && difftime(time(NULL), inicio) < 4)
            {
                Mensagem resp;
                int status = recebeMensagem(soquete, &resp);
                if (status != -1 &&
                    resp.MarcadorInicio == INICIO &&
                    resp.Sequencia == md.Sequencia)
                {
                    if (resp.Tipo == ACK)
                        enviado = 1;
                }
            }

            if (!enviado)
                printf("Timeout aguardando ACK de bloco de dados. Reenviando...\n");
        }
        *seqPtr = (*seqPtr + 1) % 32;
    }
    fclose(f);

    // 3) envia pacote FIM_ARQUIVO e espera ACK/NACK
    Mensagem mF = criaMensagem("", FIM_ARQUIVO, *seqPtr, 0);
    acknowledged = 0;
    while (!acknowledged)
    {
        enviaMensagem(soquete, mF);
        inicio = time(NULL);

        while (!acknowledged && difftime(time(NULL), inicio) < 4)
        {
            Mensagem resp;
            int status = recebeMensagem(soquete, &resp);
            if (status != -1 &&
                resp.MarcadorInicio == INICIO &&
                resp.Tipo == ACK &&
                resp.Sequencia == mF.Sequencia)
            {
                acknowledged = 1;
            }
        }

        if (!acknowledged)
            printf("Timeout aguardando ACK do FIM_ARQUIVO. Reenviando...\n");
    }
    *seqPtr = (*seqPtr + 1) % 32;
}

void desenhaTabuleiro(unsigned char *mapa)
{
    printf("\n");
    for (int y = 0; y < BOARD_SIZE; y++)
    {
        for (int x = 0; x < BOARD_SIZE; x++)
        {
            putchar(mapa[y * BOARD_SIZE + x]);
            putchar(' ');
        }
        printf("\n");
    }
}

unsigned char calculaChecksum(Mensagem *m)
{
    unsigned char sum = m->Tamanho ^ m->Sequencia ^ m->Tipo;
    for (int i = 0; i < m->Tamanho; i++)
    {
        sum ^= m->Dados[i];
    }
    return sum;
}

void debugPrintMensagem(const char *prefix, Mensagem *m)
{
    printf("%s: MarcadorInicio=0x%02X, Tamanho=%u, Sequencia=%u, Tipo=0x%X, checksum=0x%02X, Dados=[",
           prefix,
           m->MarcadorInicio,
           m->Tamanho,
           m->Sequencia,
           m->Tipo,
           m->checksum);
    for (int i = 0; i < m->Tamanho; i++)
    {
        printf(" %02X", m->Dados[i]);
    }
    printf(" ]\n");
}

// Função auxiliar para imprimir o tabuleiro 8×8
void imprimeTabuleiro(const unsigned char *mapa)
{
    system("clear");
    putchar('\n');
    for (int y = 0; y < BOARD_SIZE; y++)
    {
        for (int x = 0; x < BOARD_SIZE; x++)
        {
            putchar(mapa[y * BOARD_SIZE + x]);
            putchar(' ');
        }
        putchar('\n');
    }
}

void inicializaTesouros(Posicao tesouros[])
{
    srand(time(NULL));
    int i = 0;
    int ocupado[BOARD_SIZE][BOARD_SIZE] = {0};

    // Marca origem como ocupada (0,7) pois o jogador nao pode comecar em cima (tava no enunciado)
    ocupado[0][7] = 1;

    while (i < NUM_TESOUROS)
    {
        int x = rand() % BOARD_SIZE;
        int y = rand() % BOARD_SIZE;

        if (!ocupado[x][y])
        {
            tesouros[i].x = x;
            tesouros[i].y = y;
            ocupado[x][y] = 1;
            i++;
        }
    }

    printf("Posições dos tesouros (origem em 0,7):\n");
    for (int j = 0; j < NUM_TESOUROS; j++)
    {
        int novoY = (BOARD_SIZE - 1) - tesouros[j].y;
        printf("Tesouro %d: (%d, %d)\n", j + 1, tesouros[j].x, novoY);
    }
}

int encontrouTesouro(Posicao tesouros[], int x, int y, int *id,
                     Posicao tesourosEncontrados[], int *numTesourosEncontrados)
{
    for (int i = 0; i < NUM_TESOUROS; i++)
    {
        if (tesouros[i].x == x && tesouros[i].y == y)
        {
            *id = i + 1;

            // Marca como encontrado
            tesourosEncontrados[*numTesourosEncontrados].x = x;
            tesourosEncontrados[*numTesourosEncontrados].y = y;
            (*numTesourosEncontrados)++;

            // Remove o tesouro da lista
            tesouros[i].x = -1;
            tesouros[i].y = -1;
            return 1;
        }
    }
    return 0;
}

void enviaAck(int soquete, int seq)
{
    Mensagem ack = criaMensagem("", ACK, seq, 0);
    enviaMensagem(soquete, ack);
}

void enviaNack(int soquete, int seq)
{
    Mensagem nack = criaMensagem("", NACK, seq, 0);
    enviaMensagem(soquete, nack);
}

void enviaOkAck(int soquete, int seq, const char *dados, int tamanho)
{
    Mensagem okack = criaMensagem(dados, OK_ACK, seq, tamanho);
    enviaMensagem(soquete, okack);
}

int aguardaResposta(int soquete, Mensagem *req, int seqEsperado)
{
    while (1)
    {
        int status = recebeMensagem(soquete, req);

        if (status == -1)
        {
            printf("Timeout. Reenviando requisição...\n");
            return 0; // aqui deu timeout
        }

        if (req->MarcadorInicio != INICIO)
            continue;

        if (req->Tipo == NACK)
        {
            printf("NACK recebido. Reenviando...\n");
            return 0; // fazer o reenvio da mensagem que deu NACK
        }

        if ((req->Tipo == OK_ACK || req->Tipo == TEXTO_ACK_NOME ||
             req->Tipo == IMAGEM_ACK_NOME || req->Tipo == VIDEO_ACK_NOME) &&
            req->Sequencia == seqEsperado)
        {
            return 1; // deu boa
        }
    }
}

// Função para verificar espaço livre em disco no cliente usando statvfs (estava no enunciado)
int tem_espaco_disponivel(const char *path, unsigned long tamanho_arquivo)
{
    struct statvfs stat;

    if (statvfs(path, &stat) != 0)
    {
        perror("Erro ao obter info do sistema de arquivos");
        return 0;
    }

    unsigned long espaco_livre = stat.f_bavail * stat.f_frsize;

    return espaco_livre >= tamanho_arquivo;
}

// funcao que fica do lado do cliente para receber o arquivo (a main sabe se virar nos cases)
int recebeArquivo(int soquete, const char *nomeArquivo)
{
    Mensagem req;
    unsigned long filesize = 0;
    int tamanho_recebido = 0;
    int lastSeq = -1;

    // 1) Aguarda o pacote TAMANHO com o tamanho do arquivo
    while (1)
    {
        int status = recebeMensagem(soquete, &req);
        if (status == -1)
        {
            // Timeout, deu ruim
            continue;
        }

        if (req.MarcadorInicio == INICIO && req.Tipo == TAMANHO)
        {
            if (req.Tamanho == sizeof(filesize))
            {
                memcpy(&filesize, req.Dados, req.Tamanho);
                tamanho_recebido = 1;

                // Envia ACK confirmando recebimento do dado de tamanho
                Mensagem ack = criaMensagem(NULL, ACK, req.Sequencia, 0);
                enviaMensagem(soquete, ack);
                break;
            }
            else
            {
                // deu ruim em algum momento de leitura ou chegou dado errado
                continue;
            }
        }
    }

    if (!tamanho_recebido)
    {
        printf("Erro: pacote TAMANHO não recebido corretamente.\n");
        return 0;
    }

    // 2) Verifica se tem espaço suficiente no disco antes de abrir o arquivo
    if (!tem_espaco_disponivel(".", filesize))
    {
        // Envia mensagem de erro para o servidor
        const char *erroMsg = "espaço insuficiente no cliente";
        Mensagem erro = criaMensagem(erroMsg, ERRO, req.Sequencia, strlen(erroMsg));
        enviaMensagem(soquete, erro);

        printf("Erro: espaço insuficiente para receber arquivo de %lu bytes.\n", filesize);
        return 0;
    }

    // 3) Abre o arquivo para escrita
    FILE *f = fopen(nomeArquivo, "wb");
    if (!f)
    {
        perror("Erro ao criar arquivo local");
        return 0;
    }

    // 4) Loop para receber os pacotes de dados do arquivo
    while (1)
    {
        int status = recebeMensagem(soquete, &req);
        if (status == -1)
        {
            printf("Timeout durante recebimento do arquivo. Ignorando pacote...\n");
            continue;
        }

        if (req.MarcadorInicio != INICIO)
            continue;

        if (req.Tipo == NACK)
        {
            printf("NACK recebido durante arquivo. Ignorando...\n");
            continue;
        }

        if (req.Tipo == FIM_ARQUIVO)
        {
            // Envia ACK confirmando fim do recebimento do arquivo
            enviaAck(soquete, req.Sequencia);
            printf("Transferência concluída.\n");
            break;
        }

        if (req.Tipo == DADOS && req.Sequencia != lastSeq)
        {
            fwrite(req.Dados, 1, req.Tamanho, f);
            lastSeq = req.Sequencia;
        }

        // Envia ACK geral, os ifs anteriores mudam o fluxo caso de alguma coisa de errado
        enviaAck(soquete, req.Sequencia);
    }

    fclose(f);
    return 1;
}

// funcao que lida com o tipo livre, serve mais como um tipo explicito para atualizar o tabuleiro, tambem daria para usar o proprio OK_ACK
void requisitaEstadoTabuleiro(int soquete, int *seq)
{
    Mensagem req, resp = criaMensagem("", ESTADO_TABULEIRO, *seq, 0);
    int recebeu = 0;

    while (!recebeu)
    {
        enviaMensagem(soquete, resp);
        while (1)
        {
            int status = recebeMensagem(soquete, &req);
            if (status == -1)
            {
                printf("Timeout esperando estado do tabuleiro. Reenviando...\n");
                break;
            }

            if (req.MarcadorInicio != INICIO)
                continue;

            if (req.Tipo == NACK)
            {
                printf("NACK recebido. Reenviando estado...\n");
                break;
            }

            if (req.Tipo == OK_ACK && req.Sequencia == *seq)
            {
                *seq = (*seq + 1) % 32;
                imprimeTabuleiro(req.Dados);
                recebeu = 1;
                break;
            }
        }
    }
}

// no fim do dia era soh usar o xdg-open para tudo...
void abre_com_programa_correto(const char *dir, const char *filename)
{
    // Monta o caminho
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir, filename);

    // Verifica se o arquivo tem extensão
    const char *ext = strrchr(filename, '.');
    if (!ext)
    {
        fprintf(stderr, "Não foi possível determinar extensão de %s\n", filename);
        return;
    }

    // Usa o xdg-open para tudo
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", filepath);
    system(cmd);
}

void atualizaMapa(unsigned char mapa[], int px, int py,
                  const Posicao tesourosEncontrados[], int numTesourosEncontrados,
                  const Posicao posicoesVisitadas[], int numPosicoesVisitadas)
{
    memset(mapa, '.', BOARD_SIZE * BOARD_SIZE);

    // Marca as posições visitadas
    for (int i = 0; i < numPosicoesVisitadas; i++)
    {
        int vx = posicoesVisitadas[i].x;
        int vy = posicoesVisitadas[i].y;
        if (vx >= 0 && vx < BOARD_SIZE && vy >= 0 && vy < BOARD_SIZE)
        {
            mapa[vy * BOARD_SIZE + vx] = 'o'; // caminho percorrido
        }
    }

    // Marca os tesouros encontrados
    for (int i = 0; i < numTesourosEncontrados; i++)
    {
        int tx = tesourosEncontrados[i].x;
        int ty = tesourosEncontrados[i].y;
        if (tx >= 0 && tx < BOARD_SIZE && ty >= 0 && ty < BOARD_SIZE)
        {
            mapa[ty * BOARD_SIZE + tx] = 'T'; // ou '$'
        }
    }

    // Marca a posição atual do jogador, ele sempre vai sobrescrever o que esta embaixo, entao nunca vai perder o 'O' ou o '.' que estava antes
    mapa[py * BOARD_SIZE + px] = 'P';
}

void adicionaPosicaoVisitada(Posicao posicoesVisitadas[], int x, int y, int *numPosicoesVisitadas)
{
    // Verifica se já existe
    for (int i = 0; i < *numPosicoesVisitadas; i++)
    {
        if (posicoesVisitadas[i].x == x && posicoesVisitadas[i].y == y)
            return; // Já existe, não adiciona
    }

    // Adiciona nova posição
    posicoesVisitadas[*numPosicoesVisitadas].x = x;
    posicoesVisitadas[*numPosicoesVisitadas].y = y;
    (*numPosicoesVisitadas)++;
}

void processaMovimento(int *px, int *py, int deltaX, int deltaY, Posicao *posicoesVisitadas, int *numPosicoesVisitadas,
                       unsigned char *mapa, Posicao *tesourosEncontrados, int *numTesourosEncontrados,
                       Posicao *tesouros, int soquete, Mensagem req)
{
    // Verifica checksum
    if (req.checksum != calculaChecksum(&req))
    {
        printf("Checksum inválido. Enviando NACK...\n");
        Mensagem resp = criaMensagem("Checksum inválido", NACK, req.Sequencia, strlen("Checksum inválido"));
        enviaMensagem(soquete, resp);
        return;
    }

    // Move jogador, se dentro dos limites do tabuleiro, se ele tentar se mover para fora, nada acontece
    int nx = *px + deltaX;
    int ny = *py + deltaY;
    if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE)
    {
        *px = nx;
        *py = ny;
    }

    if (!posicaoJaVisitada(posicoesVisitadas, *numPosicoesVisitadas, *px, *py))
    {
        posicoesVisitadas[*numPosicoesVisitadas].x = *px;
        posicoesVisitadas[*numPosicoesVisitadas].y = *py;
        (*numPosicoesVisitadas)++;
    }

    atualizaMapa(mapa, *px, *py, tesourosEncontrados, *numTesourosEncontrados, posicoesVisitadas, *numPosicoesVisitadas);

    int achou, id;
    achou = encontrouTesouro(tesouros, *px, *py, &id, tesourosEncontrados, numTesourosEncontrados);

    if (achou && *numTesourosEncontrados < NUM_TESOUROS && !tesouroJaEncontrado(tesourosEncontrados, *numTesourosEncontrados, *px, *py))
    {
        tesourosEncontrados[*numTesourosEncontrados].x = *px;
        tesourosEncontrados[*numTesourosEncontrados].y = *py;
        (*numTesourosEncontrados)++;
    }

    Mensagem resp;
    if (!achou)
    {
        printf("ACHOU NADA\n");
        resp = criaMensagem((char *)mapa, OK_ACK, req.Sequencia, BOARD_SIZE * BOARD_SIZE);
        enviaMensagem(soquete, resp);
    }
    else
    {
        // Encontra o nome do arquivo pelo id e envia o nome do arquivo para o cliente solicitar logo em seguida
        char found_name[256] = {0};
        char *ext = NULL;
        DIR *d = opendir("tesouros");
        if (d)
        {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL)
            {
                int file_id;
                if (sscanf(ent->d_name, "%d", &file_id) == 1 && file_id == id)
                {
                    strncpy(found_name, ent->d_name, sizeof(found_name) - 1);
                    ext = strrchr(found_name, '.');
                    if (ext)
                        ext++;
                    break;
                }
            }
            closedir(d);
        }
        // evita que o servidor quebre caso o tesouros esteja vazio
        if (strlen(found_name) == 0)
        {
            printf("Arquivo nao encontrado no diretorio 'tesouros'.\n");
            const char *erroMsg = "arquivo no servidor nao encontrado";
            resp = criaMensagem(erroMsg, ERRO, req.Sequencia, strlen(erroMsg));
            enviaMensagem(soquete, resp);
            return;
        }

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "tesouros/%s", found_name);
        FILE *f = fopen(filepath, "rb");
        if (!f)
        {
            printf("Arquivo tesouro encontrado, mas sem permissao de leitura: %s\n", filepath);
            const char *erroMsg = "arquivo sem permissao de leitura";
            resp = criaMensagem(erroMsg, ERRO, req.Sequencia, strlen(erroMsg));
            enviaMensagem(soquete, resp);
            return;
        }
        fclose(f);

        resp = criaMensagem(found_name,
                            (ext && (!strcmp(ext, "jpg") || !strcmp(ext, "png"))) ? IMAGEM_ACK_NOME : (ext && !strcmp(ext, "mp4")) ? VIDEO_ACK_NOME
                                                                                                                                   : TEXTO_ACK_NOME,
                            req.Sequencia, strlen(found_name));
        enviaMensagem(soquete, resp);
    }
}

int tesouroJaEncontrado(Posicao tesourosEncontrados[], int numTesourosEncontrados, int x, int y)
{
    for (int i = 0; i < numTesourosEncontrados; i++)
    {
        if (tesourosEncontrados[i].x == x && tesourosEncontrados[i].y == y)
        {
            return 1; // ja encontrou o tesouro
        }
    }
    return 0; // não encontrado o tesouro ainda
}

int posicaoJaVisitada(Posicao posicoesVisitadas[], int numPosicoesVisitadas, int x, int y)
{
    for (int i = 0; i < numPosicoesVisitadas; i++)
    {
        if (posicoesVisitadas[i].x == x && posicoesVisitadas[i].y == y)
        {
            return 1; // posicao ja foi visitada
        }
    }
    return 0; // posicao nao foi visitada ainda
}
