# PROJETO COMPLETO — PONTE LEVADIÇA AUTOMATIZADA
### Adaptação técnica do relatório PPA Eletromecânica IFBA para os componentes: Arduino Nano + 2× NEMA17 + HC-SR04 + polias/tambores/cabos de aço + alumínio/zinco

---

## 1. RESUMO EXECUTIVO

Este documento converte o protótipo descrito no relatório PPA (que usava Arduino Uno, **motores DC + engrenagens** e chaves de fim de curso mecânicas simples) para a arquitetura solicitada: **Arduino Nano, 2× motor de passo NEMA17, sensor ultrassônico HC-SR04, tambores/carretéis, cabos de aço e polias**, com estrutura em perfil de alumínio e chapas de zinco.

A mudança de motor DC para motor de passo muda profundamente o projeto: passamos de um controle "liga/desliga com corte por chave fim de curso" para um controle **posicional em malha aberta por contagem de passos**, com máquina de estados não bloqueante, temporização de 30 s para a ponte aberta, e proteção redundante (fim de curso + timeout + validação de sensor).

Entregáveis deste pacote:
1. Este relatório técnico (Seções 1–26, conforme solicitado).
2. `firmware/ponte_levadica.ino` — firmware completo, compilável, comentado.
3. `simulacao/index.html + style.css + script.js` — simulação 3D funcional em Three.js.

---

## 2. ANÁLISE DO RELATÓRIO FORNECIDO (PPA IFBA)

O relatório fornecido é um **relatório de gestão de projeto acadêmico** (PPA), não um relatório de engenharia detalhada. Ele contém:

- Viabilidades (técnica, financeira, ambiental etc.), Canvas, RACI, SWOT, FMEA, Gantt — **gestão**, não dimensionamento.
- Especificação de hardware **diferente e incompatível** com a sua solicitação:
  - Motor: **DC + engrenagens**, não NEMA17.
  - Controlador: **Arduino Uno/R3**, não Nano.
  - Fim de curso: **chaves mecânicas de fim de curso**, sem carretéis/cabos de aço.
  - Sinalização: **semáforo de LEDs**, não mencionado na sua especificação (mantido como opcional).
- Fluxograma (Fig. 2) descreve corretamente a lógica de alto nível (detectar → abrir → aguardar passagem → fechar), que é **reaproveitável conceitualmente**, mas seus blocos ("Ligar Motor DC", "Ler Chave Fim de Curso") precisam ser reescritos para step/dir e para uma lógica de tempo fixo de 30 s (o relatório original usa "aguardar passagem do obstáculo" sem tempo fixo).
- FMEA do relatório (Seção 6) identifica riscos reais e reaproveitáveis: falha de leitura do HC-SR04 (risco 36), travamento de fim de curso (28), fiação interna (28), desgaste de cabos/polias (24). Esses riscos são usados na Seção 24 deste documento.
- BOM do relatório é referenciada por planilha externa (não acessível a partir deste texto) e é **substituída integramente** pela BOM da Seção 19.

**Conclusão da análise:** o relatório serve como referência de *processo* (metodologia PPA, FMEA, viabilidade) e de *lógica geral*, mas seu hardware é substituído por completo conforme instruído.

---

## 3. CORREÇÕES TÉCNICAS NECESSÁRIAS

### 3.1 Motor DC+engrenagens → NEMA17
Motores de passo não têm realimentação de posição nativa: cada "passo" é uma estimativa de posição, não uma garantia (pode haver perda de passo sob sobrecarga). Isso exige fins de curso físicos como referência absoluta (homing) — o relatório original já previa chaves fim de curso, então esse conceito é mantido e reforçado.

### 3.2 Arduino Uno → Nano
Mesmo microcontrolador (ATmega328P), mesma contagem de pinos utilizáveis, mesma tensão lógica (5 V). Não há perda de capacidade, apenas de forma física (Nano é SMD/menor, sem headers de shield). Todo pinout foi redesenhado para o Nano.

### 3.3 HC-SR04 — correção conceitual **[LIMITAÇÃO]**
O HC-SR04 é um sensor de **distância por tempo de voo de ultrassom** (emite um pulso de 40 kHz e mede o tempo até o eco retornar), **não** um sensor de barreira infravermelha. Portanto:
- **Não existe** "feixe cortado". O sensor não detecta interrupção de um feixe contínuo.
- A detecção correta é: "a distância medida caiu abaixo de um limiar X, de forma consistente em N leituras seguidas" → embarcação presente.
- Se fosse necessária uma lógica real de barreira (feixe interrompido), seria preciso um par emissor/receptor IR (ex.: par LED IR + fototransistor, ou sensor de barreira dedicado). **Isso não será feito** — o projeto mantém o HC-SR04 conforme sua instrução, apenas com a interpretação de funcionamento corrigida.

### 3.4 "8 kg" do motor — correção conceitual **[LIMITAÇÃO / DADO NECESSÁRIO]**
"8 kg" não é uma unidade de torque. É provavelmente uma forma comercial abreviada e ambígua, que pode significar:
- **8 kgf·cm** de torque de retenção (holding torque) — interpretação mais comum em anúncios de NEMA17 genéricos; equivale a **≈ 0,784 N·m = 78,4 N·cm**. Isso é *alto* para um NEMA17 padrão (a maioria fica entre 26 e 65 N·cm / 2,6–6,6 kgf·cm), então esse valor deveria ser conferido com desconfiança.
- **8 kgf** de força de tração — métrica às vezes usada por vendedores para atuadores lineares (fuso), não se aplica diretamente a um motor girando um tambor sem mais dados.
- Simplesmente o **peso do motor em kg** (alguns anúncios listam "peso: 0,35 kg" — "8 kg" não bate com isso, então essa hipótese é descartada).

**DADO NECESSÁRIO:** modelo exato do motor (ex.: 17HS4401, 17HS19-2004S1 etc.) ou o datasheet, para obter o torque de retenção real em N·cm.
**HIPÓTESE ADOTADA neste projeto:** torque de retenção conservador de **40 N·cm (≈ 4,08 kgf·cm)**, valor típico de NEMA17 padrão de baixo custo. Os cálculos da Seção 7 usam esse valor e mostram margem suficiente mesmo nesse cenário conservador.

---

## 4. ARQUITETURA GERAL

```
                         ARQUITETURA DO SISTEMA
┌───────────────┐     ┌───────────────────────┐     ┌────────────────────┐
│    ENTRADAS   │     │      PROCESSAMENTO     │     │      SAÍDAS        │
│               │     │                        │     │                    │
│ HC-SR04 (x1)  │────▶│                        │────▶│ Driver 1 → NEMA17 A│
│ Fim curso ABR │────▶│      Arduino Nano      │────▶│ Driver 2 → NEMA17 B│
│ Fim curso FEC │────▶│  (máquina de estados,  │     │                    │
│ Botão EMERG.  │────▶│   millis(), sem delay) │────▶│ LED status (opc.)  │
│               │     │                        │────▶│ Serial (debug)     │
└───────────────┘     └───────────────────────┘     └────────────────────┘
```

**Fluxo funcional completo:**
Embarcação detectada (3 leituras HC-SR04 válidas abaixo do limiar) → validação → estado `OPENING` → os dois NEMA17 aceleram, giram em sincronismo (mesma velocidade/mesma quantidade de passos), enrolando os cabos → tambores puxam os cabos → cabo passa pelas polias no topo da estrutura → ponte sobe → fim de curso "aberto" ou contagem de passos atinge o alvo → motores param → estado `OPEN` → timer de 30 s inicia → ao final, se a área estiver livre, estado `CLOSING` → motores giram no sentido inverso, desenrolando os cabos → ponte desce por gravidade controlada (cabo sempre tracionado) → fim de curso "fechado" → estado `IDLE`.

---

## 5. FUNCIONAMENTO DO SISTEMA — MÁQUINA DE ESTADOS

```text
IDLE            → ponte fechada, motores desligados, sensor monitorando.
DETECTING       → leitura positiva do HC-SR04, aguardando confirmação (debounce por N leituras).
OPENING         → motores acionados no sentido de subida, com rampa de aceleração/desaceleração.
OPEN            → ponte totalmente aberta, temporizador de 30 s rodando (não bloqueante, via millis()).
CLOSING         → motores acionados no sentido de descida, com verificação de área livre antes de iniciar.
ERROR           → falha detectada (timeout de movimento, leitura inválida persistente, motor não avançou).
EMERGENCY_STOP  → parada imediata por botão físico ou por condição crítica; requer reset manual.
```

**Transições relevantes:**
- `OPEN → CLOSING` só ocorre se os 30 s terminaram **E** o HC-SR04 não detecta mais embarcação nas últimas leituras. Se a embarcação ainda estiver presente ao fim dos 30 s, o temporizador é **retido** (estado permanece `OPEN`, um novo ciclo de checagem é feito a cada 1 s) até a área ficar livre — nunca a ponte inicia o fechamento com embarcação presente. Esse é o mecanismo de segurança citado na Seção 9.
- Qualquer estado → `EMERGENCY_STOP` a qualquer momento (interrupção por botão).
- `ERROR`/`EMERGENCY_STOP → IDLE` apenas após reset manual (botão ou reinício), nunca automaticamente — evita reabertura indevida após falha.

---

## 6. PROJETO MECÂNICO

### 6.1 Configuração adotada — HIPÓTESE ADOTADA
Como o relatório de origem não especifica a geometria da ponte (bascule com dobradiça vs. elevação vertical), e como os componentes pedidos (2 tambores, cabos, polias) favorecem simetria, adota-se:

**Ponte tipo elevação vertical assistida por 2 pontos** (um tambor/motor de cada lado), com o tabuleiro subindo verticalmente entre duas torres, guiado por trilhos laterais, cabo de aço de cada lado passando por uma polia no topo da torre e descendo até o tambor do motor correspondente.

*(Caso o projeto real seja do tipo bascule com dobradiça em uma extremidade, a mesma lógica de firmware se aplica; troque apenas a fórmula de força pela análise de torque em torno do eixo de rotação — indicado na Seção 7.4.)*

### 6.2 Representação ASCII do mecanismo

```text
                     TOPO DA TORRE (POLIAS FIXAS)
        ┌───────────────────────────────────────────────┐
        │        ○ polia A                  polia B ○   │
        │        │                                   │   │
        │        │  cabo de aço A         cabo B      │   │
        │        │                                   │   │
        │   ┌────┴────┐                     ┌────────┴┐  │
   guia │   │         │                     │         │  │ guia
   lat. │   │  TABULEIRO DA PONTE (móvel)    │         │  │ lat.
        │   │  perfil alumínio + chapa zinco │         │  │
        │   └─────────┘                     └─────────┘  │
        │                                                 │
        └───────────────────────────────────────────────┘
                 │                                │
             ┌───┴───┐                        ┌───┴───┐
             │TAMBOR A│◄── eixo acoplado ──►  │TAMBOR B│
             └───┬───┘                        └───┬───┘
                 │                                │
             ┌───┴────┐                       ┌───┴────┐
             │ NEMA17 A│                       │ NEMA17 B│
             └────────┘                       └────────┘
```

- **Polias (2 un.):** fixas no topo de cada torre, redirecionam o cabo da vertical (tabuleiro) para a vertical (tambor), reduzindo desgaste por dobra abrupta. Usar polia com canaleta (gorge) compatível com o diâmetro do cabo, para impedir que o cabo escape lateralmente.
- **Tambores/carretéis (2 un.):** montados diretamente no eixo de cada NEMA17 (sem redução, conforme cálculo da Seção 7 mostrar viabilidade). Devem ter canaleta helicoidal para que o cabo se enrole de forma ordenada (evita sobreposição/embaraço).
- **Cabo de aço:** um por lado, fixado por braçadeira tipo "grampo para cabo" (cable clamp) no tabuleiro em uma extremidade e preso ao tambor por parafuso de fixação na outra.
- **Guias laterais:** trilhos de alumínio (cantoneira ou perfil U) para impedir que o tabuleiro balance/rotacione durante a subida — crítico para não desalinhar os dois lados.
- **Sincronismo mecânico:** os dois tambores devem ter o **mesmo diâmetro exato** (mesma bitola de fabricação) — qualquer diferença de diâmetro causa velocidade linear diferente entre os dois lados mesmo com passos idênticos, inclinando o tabuleiro.
- **Tensão do cabo:** ao montar, os cabos devem partir sob leve pré-tensão (pequeno peso ou ajuste manual) para que nenhum lado "sobre" folga antes do outro no primeiro passo.

---

## 7. CÁLCULOS MECÂNICOS

**[HIPÓTESE ADOTADA — a confirmar com a montagem real]**

| Parâmetro | Valor adotado | Origem |
|---|---|---|
| Massa do tabuleiro (perfil Al + chapa zinco) | 1,2 kg | Hipótese (escala de protótipo de bancada) |
| Torque de retenção NEMA17 | 40 N·cm (conservador) | Hipótese — ver Seção 3.4 |
| Raio do tambor | 1,5 cm | Hipótese de projeto (a validar) |
| Nº de cabos que sustentam o peso | 2 (um por lado) | Definição de projeto |

### 7.1 Diferença entre unidades (obrigatório conforme Seção 11 do pedido)
- **kg** → unidade de massa, não de força nem de torque.
- **kgf** → força equivalente ao peso de 1 kg sob gravidade padrão: 1 kgf ≈ 9,81 N.
- **kgf·cm** → unidade de torque "comercial": força de 1 kgf aplicada a 1 cm de braço. 1 kgf·cm ≈ 0,0981 N·m ≈ 9,81 N·cm.
- **N·cm** → torque em unidades SI derivadas (mais usado em datasheets sérios de motor de passo).
- **N·m** → unidade SI padrão de torque. 1 N·m = 100 N·cm.

Conversão usada: **Torque[N·cm] = Torque[kgf·cm] × 9,81**

### 7.2 Força necessária no cabo
Peso do tabuleiro: `P = m × g = 1,2 kg × 9,81 m/s² = 11,77 N`
Dividido entre 2 cabos (um por lado): `F_cabo = 11,77 / 2 ≈ 5,89 N` por lado (cenário estático ideal, sem atrito de guia).
Aplicando fator de segurança de 2,5× para atrito das guias, inércia de partida e folgas mecânicas: `F_projeto ≈ 14,7 N` por lado.

### 7.3 Torque necessário no tambor
`Torque = Força × Raio`
`Torque = 14,7 N × 0,015 m = 0,22 N·m = 22 N·cm` por motor.

### 7.4 Comparação com o torque disponível
Hipótese conservadora de torque do NEMA17: **40 N·cm**.
Torque necessário calculado: **≈ 22 N·cm**.
**Margem de segurança resultante: ≈ 1,8× (82% de folga)** sobre a hipótese conservadora — **plausível sem redução mecânica**, desde que:
- a massa real do tabuleiro não ultrapasse ~2,2 kg (recalculável linearmente), e
- o torque real do motor não seja inferior a ~25 N·cm.

**LIMITAÇÃO:** este cálculo ignora atrito nas polias, curvatura do cabo e possível desalinhamento — validar experimentalmente no Teste 8 (Seção 17) com um dinamômetro/balança de mola antes da montagem final. Se o torque real do motor (após confirmação do datasheet) for inferior a ~25 N·cm, as opções de mitigação, em ordem de preferência, são:
1. Reduzir o raio do tambor (aumenta força disponível, mas reduz velocidade de subida e aumenta nº de voltas/passos necessários).
2. Adicionar uma segunda volta de cabo/polia móvel (vantagem mecânica 2:1), dobrando a força disponível à custa de metade da velocidade.
3. Reduzir a massa do tabuleiro (perfil mais leve, chapa mais fina).
4. Como último recurso, redução mecânica por polia dupla no próprio eixo do motor (correia/engrenagem) — evitar se possível, pois adiciona folga e complexidade.

### 7.5 Passos, velocidade e tempo de abertura
- Passos por volta do motor: **200** (1,8°/passo), valor típico — **confirmar com o datasheet real do motor adquirido**.
- Microstepping sugerido: 1/8 (ver Seção 9) → **1600 micropassos/volta**.
- Comprimento de cabo por volta do tambor: `C = 2 × π × r = 2 × π × 0,015 = 0,0942 m ≈ 9,42 cm/volta`.
- Adotando altura de elevação da ponte de **15 cm** (hipótese de escala de protótipo):
  `Nº de voltas = 15 / 9,42 ≈ 1,59 volta`
  `Nº de passos = 1,59 × 200 ≈ 318 passos (modo passo inteiro)` ou `≈ 2544 micropassos (1/8 microstep)`.
- Velocidade angular adotada (segurança/suavidade): 60 RPM = 1 rev/s → tempo de abertura ≈ **1,6 s de movimento efetivo**, mais rampas de aceleração/desaceleração (~0,3–0,5 s cada) → **tempo total estimado de abertura ≈ 2,2–2,6 s**.

**LIMITAÇÃO:** como o tambor é cilíndrico de raio constante (não cônico), o comprimento de cabo por volta é constante — **não há o efeito de "raio variável"** citado na Seção 15 do pedido nesse projeto (isso só ocorreria se o cabo se sobrepusesse em múltiplas camadas). Recomenda-se dimensionar o tambor comprido o suficiente para que o cabo enrole em **uma única camada**, eliminando esse erro por completo.

---

## 8. PROJETO ELÉTRICO

### 8.1 Arquitetura de alimentação

```text
                    FONTE 12V (motores/drivers)      FONTE 5V (lógica, via USB ou regulador)
                            │                                   │
                ┌───────────┼───────────┐                       │
                │                       │                       │
           Driver 1 (VMOT)        Driver 2 (VMOT)          Arduino Nano (5V)
                │                       │                       │
             NEMA17 A                NEMA17 B              HC-SR04 (VCC 5V)
                                                             Fins de curso (5V, pull-down)

   GND de TODAS as fontes deve ser interligado num único ponto comum (GND comum).
```

- **Arduino Nano:** alimentado via USB (durante testes) ou via pino VIN com fonte 7–12 V regulada, separada da fonte de motor.
- **Drivers dos motores:** alimentação de potência (VMOT) em **12 V**, dimensionada pela corrente de fase do motor (ver Seção 10). **Nunca** alimentar VMOT a partir do pino 5V do Arduino — o Nano não fornece corrente suficiente e pode ser danificado.
- **HC-SR04:** 5 V, tirado do trilho lógico do Arduino (baixo consumo, ~15 mA).
- **Fins de curso:** 5 V, com resistor de pull-down (ou usar `INPUT_PULLUP` interno do Arduino e lógica invertida).
- **Capacitor de desacoplamento:** eletrolítico 100–470 µF entre VMOT e GND, próximo a cada driver, para absorver picos de corrente no chaveamento dos motores (reduz ruído que poderia causar reset do Arduino).
- **Fusível:** recomendado um fusível de 2–3 A na linha de 12V, antes dos drivers, como proteção contra curto.
- **Interferência eletromagnética:** manter os fios de STEP/DIR (sinal) fisicamente afastados e, se possível, trançados, dos fios de potência do motor (VMOT/coils), para reduzir acoplamento de ruído no sinal digital.

---

## 9. ESCOLHA DOS DRIVERS

| Driver | Corrente máx. | Microstepping | Observação |
|---|---|---|---|
| A4988 | ~1,2 A (contínua, sem dissipador) | até 1/16 | Mais barato, mais ruidoso, sensível a superaquecimento |
| DRV8825 | ~1,5–2,2 A (com dissipador) | até 1/32 | Boa opção custo/benefício |
| TMC2208 | ~1,2–2 A | até 1/256 (interpolado) | Silencioso (StealthChop), ótimo para protótipo educacional |
| TMC2209 | ~2 A | até 1/256 | Como o 2208 + StallGuard (detecção de bloqueio) |

**Recomendação: DRV8825** (equilíbrio entre custo, disponibilidade em lojas nacionais e corrente suportada para um NEMA17 típico de ~1,2–1,7 A/fase), com **microstepping em 1/8** definido por jumpers/pinos MS1-MS3 fixos (não controlados por software, decisão de hardware).

Interface de controle (comum a todos os drivers acima):
- `STEP`: pulso digital, cada borda de subida = 1 (micro)passo.
- `DIR`: nível lógico define sentido de giro.
- `ENABLE`: ativo baixo (LOW = driver habilitado) na maioria dos módulos — **confirmar na serigrafia da placa comprada**, pois varia entre fabricantes.
- `GND` lógico do driver deve ser comum ao GND do Arduino.
- Lógica de sinal: 5 V (compatível nativamente com o Nano/ATmega328P).
- Dissipação térmica: A4988/DRV8825 aquecem sob carga — recomenda-se dissipador de alumínio colado ao chip do driver, e corrente limitada via trimpot conforme fórmula do datasheet (ex., DRV8825: `Vref = Imax × 0,5`).

**DADO NECESSÁRIO:** corrente de fase exata do motor NEMA17 adquirido (no datasheet), para ajustar corretamente o trimpot do driver — ajuste incorreto pode superaquecer o motor ou perder torque.

---

## 10. PINAGEM COMPLETA (Arduino Nano)

| Componente | Pino Arduino Nano | Função |
|---|---:|---|
| HC-SR04 TRIG | D2 | Trigger (saída) |
| HC-SR04 ECHO | D3 | Echo (entrada, aceita interrupção) |
| Driver Motor A — STEP | D4 | Pulso de passo |
| Driver Motor A — DIR | D5 | Direção |
| Driver Motor B — STEP | D6 | Pulso de passo |
| Driver Motor B — DIR | D7 | Direção |
| ENABLE (comum aos 2 drivers) | D8 | Habilitação (LOW = ativo) |
| Fim de curso — ABERTO | D9 | Segurança (INPUT_PULLUP) |
| Fim de curso — FECHADO | D10 | Segurança (INPUT_PULLUP) |
| Botão de parada de emergência | D11 | Segurança (INPUT_PULLUP) |
| LED de status (opcional) | D12 | Indicação visual de estado |
| — | D13 / RX0 / TX0 | Reservados (LED onboard / Serial USB — não usar para periféricos) |

Pinos A0–A7 ficam livres para expansão futura (ex.: segundo HC-SR04, sensor de corrente, etc.).

---

## 11. ALIMENTAÇÃO (resumo)

- Arduino Nano: 5 V via USB (bancada/testes) ou 7–12 V via VIN (campo).
- Drivers/motores: fonte externa 12 V, dimensionada em corrente para 2× corrente de fase do motor + margem (mínimo recomendado: fonte de 12V/3A).
- GND comum obrigatório entre fonte de motor, drivers e Arduino.
- Capacitores de desacoplamento junto a cada driver.
- Fusível de proteção na linha de 12 V.

---

## 12. MÁQUINA DE ESTADOS (ver também Seção 5)

Ver diagrama e tabela de transições na Seção 5. A implementação em firmware usa um `enum SystemState` e uma função `switch` chamada a cada iteração do `loop()`, sem `delay()` bloqueante — toda temporização usa `millis()`.

---

## 13. ALGORITMO (alto nível)

```text
loop():
  ler todos os sensores de segurança (fim de curso, botão emergência) — sempre, em qualquer estado
  se emergência ativada → forçar estado EMERGENCY_STOP, parar motores imediatamente

  switch (estado atual):
    IDLE:
      ler HC-SR04 (não bloqueante, uma leitura por ciclo)
      se leitura válida e < limiar por N leituras consecutivas → estado = DETECTING

    DETECTING:
      confirmar leituras adicionais (mais debounce)
      se confirmado → estado = OPENING, iniciar movimento dos 2 motores
      se leitura não se confirma → estado = IDLE (falso alarme)

    OPENING:
      chamar run() dos dois AccelStepper a cada ciclo
      monitorar timeout de movimento e fim de curso ABERTO
      se ambos motores completaram os passos-alvo OU fim de curso ABERTO ativado → parar motores, estado = OPEN, marcar tempo_abertura = millis()
      se timeout excedido sem completar → estado = ERROR

    OPEN:
      se (millis() - tempo_abertura) >= 30000:
          reler HC-SR04
          se área livre → estado = CLOSING, iniciar movimento reverso
          senão → permanecer em OPEN, reler a cada 1 s (não reinicia os 30s do zero, apenas aguarda)

    CLOSING:
      chamar run() dos dois motores
      monitorar timeout e fim de curso FECHADO
      se completou OU fim de curso FECHADO → parar motores, estado = IDLE
      se timeout → estado = ERROR

    ERROR / EMERGENCY_STOP:
      motores desabilitados (ENABLE em nível inativo)
      aguardar reset manual (ex.: reenergizar ou comando serial "RESET")
```

---

## 14. CÓDIGO COMPLETO ARDUINO NANO

Ver arquivo anexo `firmware/ponte_levadica.ino`. Estrutura do arquivo:

```cpp
// CONFIGURAÇÕES E CONSTANTES AJUSTÁVEIS
// DEFINIÇÃO DE PINOS
// BIBLIOTECAS
// ESTADOS DO SISTEMA (enum)
// OBJETOS GLOBAIS (AccelStepper x2, NewPing/leitura manual HC-SR04)
// VARIÁVEIS DE ESTADO
// setup()
// loop()
// FUNÇÕES DO SENSOR (leitura HC-SR04 não bloqueante, filtro por mediana)
// FUNÇÕES DE MOVIMENTO (iniciarAbertura, iniciarFechamento, motoresPararEDesabilitar)
// FUNÇÕES DE SEGURANÇA (checarEmergencia, checarFinsDeCurso, checarTimeout)
// FUNÇÕES DE ESTADO (transicaoPara, tratarEstadoAtual)
// FUNÇÕES DE DIAGNÓSTICO (prints Serial, modo de teste manual por comando serial)
```

**Biblioteca utilizada: `AccelStepper`** (disponível no Gerenciador de Bibliotecas do Arduino IDE, por Mike McCauley). Justificativa: é leve (compatível com ATmega328P/Nano), amplamente estável, fornece rampas de aceleração/desaceleração prontas (`setAcceleration`, `setMaxSpeed`) e permite controlar 2 motores de forma independente sem bloquear o `loop()` — evitando a necessidade de reimplementar geração de pulso STEP manualmente com temporização por `micros()`. Alternativa sem biblioteca externa é possível (gerando pulsos STEP manualmente via `micros()`), mas foi descartada aqui por aumentar a complexidade do código sem ganho relevante de RAM/Flash no Nano (a AccelStepper ocupa poucos KB e cabe folgadamente nos 32 KB de Flash do ATmega328P).

**Estratégia de sincronismo escolhida: Estratégia B — controle independente por AccelStepper**, com `moveTo()` chamado simultaneamente para os dois motores com o mesmo número de passos-alvo e o mesmo perfil de velocidade/aceleração, e `run()` chamado para ambos a cada iteração do `loop()`. 

Comparação:
- **Estratégia A (STEP único para ambos os drivers):** garante sincronismo físico perfeito de pulso, mas exige que os dois drivers estejam configurados de forma absolutamente idêntica (mesmo microstepping, mesma corrente) e não permite compensação individual; qualquer diferença mecânica entre os dois lados (atrito, diâmetro real do tambor) causa inclinação da ponte sem possibilidade de correção via software.
- **Estratégia B (independente, escolhida):** permite, no futuro, adicionar realimentação (ex.: encoders ou sensores de fim de curso individuais por lado) para corrigir pequenas diferenças entre os lados — hoje o firmware já lê os fins de curso separadamente e pode ser estendido para parar cada motor individualmente ao atingir seu próprio fim de curso, algo impossível na Estratégia A.

**Risco residual:** por não haver realimentação de posição real (encoders), perda de passo em um dos motores (por exemplo, por atrito assimétrico) causará inclinação **sem que o firmware perceba imediatamente** — mitigado por: (1) fins de curso individuais por lado, que corrigem o erro a cada ciclo completo de abertura/fechamento; (2) `STEP_TIMEOUT_MS`, que aborta o movimento e vai para `ERROR` se um motor não reportar progresso dentro do tempo esperado — **recomenda-se fortemente**, como melhoria futura, adicionar um encoder rotativo simples em cada eixo (Seção 25) para detecção real de perda de passo (`stall`), especialmente se for usado driver TMC2209 (tem StallGuard nativo).

---

## 15. EXPLICAÇÃO DO CÓDIGO (resumo — comentários completos estão no .ino)

- **Leitura do HC-SR04** é feita com `pulseIn()` com timeout definido (evita travar o `loop()` caso o eco nunca retorne — situação de falha de sensor tratada na Seção 16), e um filtro simples de mediana de 3 leituras para reduzir ruído/leituras espúrias.
- **Debounce de detecção:** exige `LEITURAS_CONFIRMACAO` (constante, padrão 3) leituras consecutivas abaixo do limiar antes de mudar de `IDLE` para `DETECTING → OPENING`, evitando disparos por ruído momentâneo.
- **Temporizador de 30 s** implementado com `millis()` (não bloqueante): `if (millis() - marcoTempoAberto >= TEMPO_PONTE_ABERTA)`.
- **Retenção de fechamento:** se a embarcação ainda for detectada ao fim dos 30 s, o firmware **não reinicia a contagem do zero** (evitaria fechamento indefinidamente adiado por leituras falsas); ele apenas aguarda 1 s e reavalia — assim que a área estiver livre, fecha imediatamente.
- **ENABLE dos drivers** é mantido em nível ativo apenas durante os movimentos (`OPENING`/`CLOSING`); em `IDLE`/`OPEN`/`ERROR`/`EMERGENCY_STOP` os drivers são desabilitados, reduzindo aquecimento dos motores e dissipando o efeito de "motor travado consumindo corrente parado".

---

## 16. PROCEDIMENTO DE CALIBRAÇÃO

1. **Distância do HC-SR04:** com a via livre, anotar a leitura estável (deve corresponder à distância real medida com trena); ajustar `DISTANCIA_DETECCAO` para um valor menor que a menor distância possível de um veículo/embarcação real na pista, com margem de segurança.
2. **Posição fechada:** com a ponte fisicamente fechada, ajustar o fim de curso `FECHADO` até que ele acione exatamente nesse ponto (sem folga adicional).
3. **Posição aberta:** com a ponte fisicamente na posição totalmente aberta desejada, ajustar o fim de curso `ABERTO` da mesma forma.
4. **Quantidade de passos:** rodar o `MODO_DIAGNOSTICO` (Seção 21) para mover motor a motor em pequenos incrementos e contar os passos reais até acionar cada fim de curso; usar esse valor para `PASSOS_ABERTURA`.
5. **Velocidade:** iniciar com valor baixo (ex.: 200 passos/s) e aumentar gradualmente até notar perda de passo (motor "pula"/vibra sem girar) — usar 70% desse valor limite como `VELOCIDADE_MOTOR` definitiva.
6. **Aceleração:** ajustar `ACELERACAO_MOTOR` para eliminar solavancos no início/fim do movimento (valor menor = mais suave, porém mais lento).
7. **Tensionamento dos cabos:** com a ponte fechada, os cabos devem estar tracionados sem folga, mas sem pré-carga excessiva; ajustar manualmente nos grampos de fixação.
8. **Sincronização dos motores:** com a ponte parada, comparar visualmente/manualmente os dois lados a cada ciclo completo; se um lado avançar mais que o outro ao longo de vários ciclos, verificar diâmetro real do tambor e atrito da guia daquele lado.
9. **Tempo de abertura:** cronometrar o tempo real do primeiro ao último passo e comparar com o estimado (Seção 7.5); ajustar velocidade se necessário.
10. **Tempo de fechamento:** mesmo procedimento do item 9, no sentido inverso.

---

## 17. PLANO DE TESTES

| Teste | Descrição | O que observar |
|---|---|---|
| 1 | Arduino sem motores | Boot correto, mensagens Serial de inicialização, leitura de fim de curso e botão de emergência respondendo |
| 2 | HC-SR04 isolado | Leituras estáveis, sem valores absurdos (0 ou > 400 cm), resposta a objeto se aproximando |
| 3 | Driver isolado (sem motor acoplado à carga) | STEP/DIR fazem o motor girar livremente nos dois sentidos, sem aquecimento excessivo |
| 4 | Motor A isolado (com tambor, sem cabo sob carga real) | Nº de passos corresponde à rotação esperada, sem perda de passo audível |
| 5 | Motor B isolado | Idem ao Teste 4 |
| 6 | Dois motores juntos (sem carga mecânica real) | Ambos partem/param no mesmo instante, mesma velocidade audível |
| 7 | Sistema mecânico sem ponte (cabo e tambor livres) | Enrolamento uniforme, cabo não escapa da polia, sem embaraço |
| 8 | Ponte com carga reduzida (peso de teste menor que o real) | Motores sobem a carga sem perda de passo — validar torque real medido vs. calculado (Seção 7.4) |
| 9 | Sistema completo (ciclo real: detecção → abertura → 30s → fechamento) | Todos os estados ocorrem na ordem correta, tempos batem com o calibrado |
| 10 | Condição de emergência (botão pressionado durante abertura) | Motores param imediatamente, sistema entra em `EMERGENCY_STOP` e não retoma sozinho |

---

## 18. DIAGNÓSTICO DE FALHAS

| Falha | Causa possível | Sintoma | Solução |
|---|---|---|---|
| Motor não gira | ENABLE no nível errado, fiação STEP/DIR trocada, corrente do driver muito baixa | Nenhum movimento, driver não aquece | Checar polaridade do ENABLE, checar Vref do driver, checar fiação |
| Motor perde passo | Torque insuficiente, velocidade/aceleração excessiva, corrente subdimensionada no driver | Ruído de "vibração" sem giro completo, posição final incorreta | Reduzir velocidade/aceleração, aumentar corrente do driver, revisar cálculo de torque (Seção 7) |
| Ponte inclina | Diâmetros de tambor diferentes, atrito assimétrico nas guias, perda de passo em um lado | Um lado sobe mais rápido/mais alto que o outro | Verificar fins de curso individuais, igualar tambores, lubrificar guias |
| Sensor dispara sozinho | Ruído elétrico, reflexão de objeto próximo fixo, mau contato | Estado `DETECTING` sem embarcação real | Aumentar `LEITURAS_CONFIRMACAO`, blindar/afastar fiação de potência, checar soldas |
| Arduino reinicia sozinho | Ruído no chaveamento dos motores acoplando na alimentação lógica, GND não comum | Reset espontâneo durante `OPENING`/`CLOSING` | Adicionar capacitores de desacoplamento, garantir GND comum único, separar fisicamente fiação de sinal e potência |
| Cabo escapa da polia | Polia sem canaleta adequada, desalinhamento do cabo em relação à polia | Cabo salta para fora do sulco da polia durante o movimento | Usar polia com canaleta mais profunda, realinhar geometria de tração |
| Fim de curso não aciona | Posição mal calibrada, chave com folga excessiva | Motor tenta girar além do limite físico | Recalibrar (Seção 16), verificar timeout de movimento como proteção secundária |
| HC-SR04 trava (echo nunca retorna) | Cabo solto, sensor danificado, obstáculo criando eco múltiplo | `pulseIn()` estoura timeout, leitura fica "presa" | Timeout definido em código evita travar o loop; se persistir, sinalizar `ERROR` e checar sensor fisicamente |

---

## 19. LISTA COMPLETA DE MATERIAIS (BOM)

### Eletrônica
| Item | Qtde | Especificação | Função | Observação |
|---|---|---|---|---|
| Arduino Nano (ATmega328P) | 1 | Original ou clone CH340 | Controlador principal | Confirmar driver USB do clone no PC de programação |
| Driver DRV8825 (ou equiv.) | 2 | Módulo com dissipador | Controle de corrente/microstepping dos motores | Ajustar Vref antes de energizar motor |
| HC-SR04 | 1 (ou 2, ver Seção "Sensoriamento") | Módulo padrão | Medição de distância | — |
| Fim de curso (chave microswitch) | 2 | Tipo alavanca, NA/NF | Referência de posição aberta/fechada | — |
| Botão de emergência | 1 | Push-button NF, preferencialmente com trava | Corte imediato dos motores | — |
| Capacitor eletrolítico | 2 | 220–470 µF / 25V | Desacoplamento junto aos drivers | — |
| Fusível + porta-fusível | 1 | 2–3 A | Proteção da linha 12V | — |
| Jumpers/cabo flexível | várias | 22–24 AWG | Conexões de sinal | — |

### Mecânica
| Item | Qtde | Especificação | Função | Observação |
|---|---|---|---|---|
| Motor de passo NEMA17 | 2 | Confirmar torque real (Seção 3.4) | Atuadores principais | **DADO NECESSÁRIO: datasheet** |
| Tambor/carretel | 2 | Raio ~1,5 cm, canaleta helicoidal, acoplado ao eixo do motor | Enrolamento do cabo | Diâmetro idêntico entre os dois |
| Polia com canaleta | 2 | Diâmetro compatível com o cabo | Redirecionamento do cabo | — |
| Cabo de aço | ~2–3 m | Diâmetro fino (ex.: 1,5–2 mm), flexível (7×7 ou 7×19 fios) | Tração da ponte | Cabo mais flexível reduz fadiga em curvas apertadas |
| Grampos para cabo (cable clamp) | 4+ | Compatível com o diâmetro do cabo | Fixação nas extremidades | — |

### Estrutura
| Item | Qtde | Especificação | Função | Observação |
|---|---|---|---|---|
| Perfil estrutural de alumínio | conforme projeto | Perfil tipo cantoneira/perfil U leve | Estrutura das torres e guias | — |
| Chapa de zinco | conforme projeto | Fina, para tabuleiro/revestimento | Tabuleiro da ponte | Considerar peso no cálculo (Seção 7) |

### Fixadores
| Item | Qtde | Especificação |
|---|---|---|
| Parafusos M3/M4 + porcas | diversos | Fixação geral de estrutura e componentes |
| Suporte para motor NEMA17 | 2 | Bracket compatível com furação padrão NEMA17 |

### Fiação
| Item | Qtde |
|---|---|
| Fio flexível para potência do motor | conforme distância |
| Conectores JST/terminais | conforme necessidade |

### Segurança
| Item | Qtde | Observação |
|---|---|---|
| Botão de emergência | 1 | Já listado em Eletrônica |
| Fusível | 1 | Já listado em Eletrônica |

### Ferramentas
Multímetro, ferro de solda, alicate de corte/desencape, chave de fenda/Phillips, trena, paquímetro (para medir diâmetro real do cabo/tambor).

### Componentes opcionais
| Item | Função |
|---|---|
| Segundo HC-SR04 | Redundância de detecção (ver Seção "Sensoriamento") |
| Encoder rotativo por eixo | Realimentação real de posição (melhoria futura, Seção 25) |
| LED de sinalização (semáforo) | Reaproveitado do relatório original, opcional |

---

## 20. SENSORIAMENTO (detalhamento)

- **Quantidade recomendada:** 1 HC-SR04 é suficiente para o protótipo funcional pedido; **2 unidades** (uma de cada lado da via de navegação, ou uma voltada para cada sentido) são recomendadas como redundância, dado que o próprio relatório original (FMEA) apontou falha de leitura do sensor como o risco mais crítico (índice 36).
- **Leitura sequencial:** se 2 sensores forem usados, dispare-os um de cada vez (nunca simultaneamente), com um pequeno intervalo (ex.: 60 ms) entre eles, para evitar que o pulso ultrassônico de um seja captado como eco pelo outro (crosstalk).
- **Distância mínima confiável do HC-SR04:** ~2 cm. **Distância máxima confiável:** ~400 cm (mas com maior erro acima de ~200 cm).
- **Diferenciação de eventos:**
  - *Ausência de embarcação:* leitura estável e maior que `DISTANCIA_DETECCAO`.
  - *Embarcação detectada:* leitura estável e menor que `DISTANCIA_DETECCAO` por N leituras seguidas.
  - *Leitura inválida:* `pulseIn()` retorna 0 (timeout/echo nunca voltou) — descartar a leitura, não contar como detecção nem como ausência.
  - *Objeto permanente (falso positivo persistente):* leitura baixa e estável indefinidamente, mesmo após o tempo de trânsito esperado de uma embarcação — sinalizar como possível erro de instalação/obstrução física, não abrir a ponte indefinidamente parada em `OPEN` (usar um teto máximo razoável de espera, com alerta via Serial, tratado como melhoria futura de UX).
  - *Ruído:* leituras que variam bruscamente e sem padrão entre ciclos — mitigado pelo filtro de mediana de 3 leituras.

---

## 21. DIAGRAMA ELÉTRICO EXPANDIDO

```text
Fonte 12V (+/-)
 │
 ├── Arduino Nano (VIN + GND)         [se alimentado externamente]
 │
 ├── Driver A (VMOT, GND) ── STEP→D4  DIR→D5  ENABLE→D8 (comum)
 │        └── NEMA17 A (fase A1/A2/B1/B2)
 │
 ├── Driver B (VMOT, GND) ── STEP→D6  DIR→D7  ENABLE→D8 (comum)
 │        └── NEMA17 B (fase A1/A2/B1/B2)
 │
 └── GND comum de tudo (Arduino, Drivers, Fonte)

Arduino Nano (5V/GND lógico):
 ├── HC-SR04: VCC→5V, TRIG→D2, ECHO→D3, GND→GND
 ├── Fim de curso ABERTO: um terminal→D9 (INPUT_PULLUP), outro terminal→GND
 ├── Fim de curso FECHADO: um terminal→D10 (INPUT_PULLUP), outro terminal→GND
 ├── Botão emergência: um terminal→D11 (INPUT_PULLUP), outro terminal→GND
 └── LED status (opcional): D12 → resistor 220Ω → LED → GND
```

---

## 22. DIAGNÓSTICO E MODO DEBUG (via Serial Monitor, 9600 baud)

Mensagens já implementadas no firmware:
```
[SISTEMA] Inicializando...
[SISTEMA] Homing concluido / fim de curso FECHADO confirmado
[SENSOR] Embarcacao detectada (dist=XX cm)
[SISTEMA] Iniciando abertura
[MOTOR] Abrindo ponte...
[SISTEMA] Ponte totalmente aberta
[TIMER] 30 segundos iniciados
[SISTEMA] Area ainda ocupada, aguardando liberacao para fechar
[SISTEMA] Iniciando fechamento
[SISTEMA] Ponte fechada
[ERRO] Timeout de movimento no motor A/B
[ERRO] Leitura invalida do sensor (timeout de echo)
[EMERGENCIA] Parada acionada pelo usuario
```

**Modo de diagnóstico manual** (ativado enviando `D` pelo Serial Monitor): permite comandos de texto simples para testar cada subsistema isoladamente, sem depender da máquina de estados completa:
```
S  -> imprime leitura atual do HC-SR04
A  -> avança motor A alguns passos (teste isolado)
B  -> avança motor B alguns passos (teste isolado)
L  -> imprime estado atual dos 2 fins de curso e do botão de emergência
E  -> imprime o estado atual da maquina de estados
R  -> reset manual (sai de ERROR/EMERGENCY_STOP e volta para IDLE, apos checagem de seguranca)
```

---

## 23. SIMULAÇÃO 3D

Ver pasta `simulacao/` (index.html, style.css, script.js), construída em **Three.js** (via CDN, execução 100% local no navegador, sem necessidade de servidor além de abrir o arquivo/servir localmente).

A simulação apresenta:
- Estrutura das duas torres em "perfil de alumínio" (representação geométrica simplificada) e tabuleiro em "chapa de zinco".
- Polias (toroides) no topo das torres, cabos representados como linhas que se encurtam/alongam de forma sincronizada com a rotação visual dos tambores.
- Tambores girando de fato (rotação visual do cilindro) enquanto o cabo "enrola"/"desenrola".
- Um cubo representando uma embarcação que se aproxima pela via de navegação.
- Sensor HC-SR04 representado como um pequeno sensor no topo de uma torre, com raio de detecção desenhado.

### Interface da simulação
Botões: `INICIAR AUTO`, `PARAR`, `RESET`, `ABRIR PONTE`, `FECHAR PONTE`, `SIMULAR EMBARCAÇÃO`.
Painel de status: distância medida (simulada), estado atual (`IDLE/DETECTING/OPENING/OPEN/CLOSING`), posição da ponte (%), velocidade, cronômetro dos 30 s, status dos motores (parado/subindo/descendo).

---

## 24. RELAÇÃO ENTRE SIMULAÇÃO E PROTÓTIPO FÍSICO

| Parâmetro | Firmware (Arduino) | Simulação (JS) | Observação |
|---|---|---|---|
| Tempo de ponte aberta | `TEMPO_PONTE_ABERTA = 30000` ms | `TEMPO_PONTE_ABERTA = 30` s | Mantidos equivalentes |
| Distância de detecção | `DISTANCIA_DETECCAO` (cm) | `distanciaDeteccao` (cm) | Mesmo valor numérico usado nos dois |
| Passos por revolução | 200 (base) | não aplicável diretamente (simulação usa ângulo contínuo), mas a **duração** do movimento é equivalente | A simulação usa tempo de abertura calculado (Seção 7.5) como referência de duração da animação |
| Tempo de abertura/fechamento | derivado de passos/velocidade real | valor configurável, ajustado para bater com o valor calculado na Seção 7.5 (~2,2–2,6 s de movimento + rampas) | — |
| Velocidade do motor | `VELOCIDADE_MOTOR` (passos/s) | velocidade angular do tambor 3D | Escaladas para gerar o mesmo tempo total de curso |

Sempre que os valores das constantes ajustáveis do firmware (Seção 25/28 do pedido original) forem alterados após a calibração real, os mesmos valores devem ser replicados nas constantes equivalentes de `script.js`, para que a simulação continue representando fielmente o comportamento do protótipo físico.

---

## 25. ANÁLISE DE SEGURANÇA

- Parada de emergência física (botão) tem prioridade absoluta sobre qualquer estado — implementada por checagem no topo do `loop()`, antes de qualquer lógica de estado.
- Fins de curso são a referência **física** de posição e sempre têm prioridade sobre a contagem de passos (a contagem de passos é usada apenas como estimativa/timeout, nunca como única fonte de verdade para parar o motor).
- Timeout de movimento evita que o sistema fique "preso" indefinidamente em `OPENING`/`CLOSING` caso um fim de curso falhe mecanicamente.
- A ponte nunca inicia o fechamento com embarcação detectada — condição obrigatória verificada antes da transição `OPEN → CLOSING`.
- Após `ERROR` ou `EMERGENCY_STOP`, o sistema **não** retorna automaticamente a `IDLE` — exige reset manual, evitando reabertura/fechamento indevido após uma falha não diagnosticada.
- GND comum e capacitores de desacoplamento mitigam o risco de reset espontâneo do Arduino durante o acionamento dos motores (risco já identificado no FMEA do relatório original).

---

## 26. MELHORIAS FUTURAS

- Adicionar encoder rotativo (ou sensor de efeito Hall com ímã no eixo) em cada motor para realimentação real de posição, detectando perda de passo (stall) em tempo real — especialmente valioso se o driver escolhido for TMC2209 (StallGuard nativo).
- Adicionar segundo HC-SR04 para redundância de detecção, conforme apontado como risco crítico no FMEA original.
- Persistir o estado da ponte em EEPROM, para que, após queda de energia, o sistema saiba se a última posição conhecida era aberta ou fechada antes de tentar fazer o homing.
- Migrar o cálculo de torque (Seção 7) para valores reais medidos com dinamômetro após a montagem física do protótipo, substituindo as hipóteses adotadas.
- Avaliar controle de corrente adaptativa (reduzir corrente do driver quando o motor está parado/segurando posição) para reduzir aquecimento nos períodos longos do estado `OPEN`.

---

## 27. CONCLUSÃO TÉCNICA

A substituição de motores DC por NEMA17 e a adoção de tambores/cabos/polias em vez de engrenagens é **tecnicamente viável** dentro das hipóteses conservadoras adotadas (margem de torque de ~1,8× mesmo no cenário mais pessimista para o torque do motor), mas depende de duas confirmações antes da montagem final: (1) o torque de retenção real do motor NEMA17 comprado (o rótulo "8 kg" é ambíguo e não deve ser usado diretamente em nenhum cálculo), e (2) a massa real do tabuleiro após a fabricação em alumínio/zinco. O firmware proposto é não bloqueante, orientado a máquina de estados, com múltiplas camadas de segurança (fins de curso, timeout, debounce de sensor, parada de emergência, e retenção de fechamento com embarcação presente), atendendo aos requisitos de robustez do pedido original. A simulação 3D acompanha os mesmos parâmetros de tempo do firmware, servindo como ferramenta de validação visual antes e durante a montagem física.
