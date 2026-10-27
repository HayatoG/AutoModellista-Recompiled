# M9: resolução interna maior (`--internal-res 1|2|3|4`)

Só no `--gfx gpu` com o ubershader HLSL (D3D12 e Vulkan; no Metal com o shader à mão a opção é ignorada com aviso).
Padrão 1: nenhum caminho novo executa e a saída é idêntica byte a byte (PNG do `--shot` e timebase).

## Como funciona

- Os vértices continuam em **pixels nativos** do EFB (`gx_gpu.c put_vertex`) e o vertex shader faz `x/320-1` e
  `1-y/264` em NDC. A resolução vem só do **tamanho dos alvos**: as duas texturas do EFB (R32_UINT, cor e Z) e o alvo
  falso do passe têm `640*N x 528*N` (`platform_gpu.c`). O HLSL não mudou (sem compilador de shader neste PC).
- O scissor é multiplicado por N em `render()`; a limpeza (`push_clear`) usa scissor nativo e entra na mesma conta. A
  limpeza do shader usa `SV_Position` e sai em N sozinha. O Z (24 bits, `noperspective`) fica correto.
- O upload do EFB da CPU (`efb_dirty`: só no reset e no `set_backend`) replica cada pixel NxN.
- **Etapa 1a** (`gx_gpu.c submit_rows_scaled`): na cópia do EFB a CPU traz as linhas em N x N e reduz por **média de
  caixa por canal** (6 bits em RGBA6, 8 nos outros; Z pela amostra do centro) para o EFB nativo; o `copy_to_xfb` e o
  `copy_to_texture` seguem como sempre. O XFB YUYV do guest, o `--shot` e o timebase não mudam; já é SSAA NxN.
- **Etapa 1b**: na cópia para o XFB, `gx_efb_copy_xfb_hires` gera também a imagem **RGBA em N x N, sem YUV**, com o
  mesmo filtro vertical e gama (rodando em `gx_raster_parallel_for`). `gx_gpu.c` guarda as 3 últimas por endereço do
  XFB. No present (thread do GX, bloco `present` de `gx_fifo.c`; sem a thread, em `capture_frame`) a janela recebe a
  imagem grande se existir para o endereço e o tamanho, senão o XFB nativo de sempre. `ensure_xfb`/blit recriam a
  textura quando o tamanho muda; reduzida na janela (menor que a imagem) o blit é linear.
- **Captura sem janela**: com `--shot` e N > 1 sai também `<arquivo>_hires.png` (o que a janela receberia).

## Limites conhecidos

| O que | Efeito em N > 1 |
| --- | --- |
| LOD por derivadas (`ddx/ddy`) | cairia a 1/N; compensado pelo bias de LOD de 8 bits de `tex_mode0` (`gpu_scale_lod_mode0`): +32 (N=2), +51 (N=3, erro de 0,01 de LOD), +64 (N=4) em 1/32 de nível |
| Neblina com ajuste de alcance | usa o x do pixel e a largura do viewport nativos: o ajuste é **desligado** (bit 10 de `FOGRANGE0`); a neblina só perde o alargamento nas bordas da tela |
| Dither Bayer 2x2 (RGBA6) | vira dither de 2x2 em pixels pequenos, e a média de caixa o apaga em grande parte; N ímpar desalinha o padrão |
| Igualdade bit a bit com o software | acaba (esperado) |
| Cópia do Z (profundidade) | reduzida pela amostra do centro do bloco |
| N = 3 e 4 | experimentais: o ROV cresce com N² em fragmentos e a redução na CPU pesa na thread do GX; 2x deve caber na RTX 2060 |

Memória (R32 = 4 B/pixel, cor + Z): 2x 10,8 MB, 3x 24,3 MB, 4x 43,3 MB na GPU; na CPU, mais o EFB grande e 3 imagens
RGBA da janela (4x: ~130 MB).

## Testes

`tests/unit/gpu_scale_test.c`: média de caixa, replicação NxN, ida e volta, escala do scissor, bias de LOD, lote do
backend com EFB grande (GPU falsa) e a imagem grande por endereço do XFB. `gpu_shader_test` segue pulado no Windows.

## Como medir

`work\run-perf\internal-res\medir.sh` (máquina livre; abre a janela): rastro por quadro de 6900 a 7499 com N = 1 2 3 3 2 1.
Comparar P50/P95 de relógio, CPU, GX ocupado, espera pelo GX, espera da GPU (`leituras do EFB`) e imagens novas/s.
Capturas: `work\run-perf\internal-res\shots.sh <N>` (headless, sem janela).
