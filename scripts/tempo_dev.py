"""Tempo de desenvolvimento acumulado até cada marco (para os roteiros).

    python3 -I scripts/tempo_dev.py [--pausa MIN] "<glob de .jsonl>" [...] -- "Marco=AAAA-MM-DDTHH:MM" [...]

Junta os horários de todas as respostas (sessões e subagentes, de todos os globs) numa linha do tempo só. Trechos
com respostas a menos de --pausa minutos uma da outra (padrão 15) contam como trabalho; intervalos maiores são pausa
(sono, espera do usuário). Para cada marco (horário de Brasília, UTC-3) imprime o tempo de calendário desde a primeira
resposta e o tempo de trabalho acumulado até ali, com IAs em paralelo contando uma vez só.
"""
import glob, json, sys
from datetime import datetime, timedelta, timezone

BRT = timezone(timedelta(hours=-3))
args = sys.argv[1:]
pausa = timedelta(minutes=15)
if args[:1] == ["--pausa"]:
    pausa = timedelta(minutes=float(args[1]))
    args = args[2:]
sep = args.index("--") if "--" in args else len(args)
globs, marcos = args[:sep], args[sep + 1:]

stamps = []
for g in globs:
    for f in glob.glob(g, recursive=True):
        for line in open(f, errors="ignore"):
            try:
                e = json.loads(line)
            except ValueError:
                continue
            m = e.get("message")
            if e.get("timestamp") and isinstance(m, dict) and m.get("usage"):
                stamps.append(datetime.fromisoformat(e["timestamp"].replace("Z", "+00:00")))
stamps.sort()
if not stamps:
    sys.exit("nenhuma resposta encontrada")

# Trechos de trabalho: respostas consecutivas a menos de `pausa` uma da outra.
trechos = []
ini = fim = stamps[0]
for t in stamps[1:]:
    if t - fim > pausa:
        trechos.append((ini, fim))
        ini = t
    fim = t
trechos.append((ini, fim))


def trabalho_ate(lim):
    total = timedelta()
    for a, b in trechos:
        if a >= lim:
            break
        total += min(b, lim) - a
    return total


def fmt(d):
    h, r = divmod(int(d.total_seconds()) // 60, 60)
    return f"{h}h{r:02d}"


print(f"início: {stamps[0].astimezone(BRT):%d/%m %H:%M} (Brasília); pausa > {pausa.total_seconds() / 60:.0f} min não conta")
print(f"{'marco':<28} {'quando':<12} {'calendário':>11} {'trabalho':>9}")
for spec in marcos + [f"agora (último registro)={stamps[-1].astimezone(BRT):%Y-%m-%dT%H:%M}"]:
    nome, quando = spec.rsplit("=", 1)
    lim = datetime.fromisoformat(quando).replace(tzinfo=BRT)
    print(f"{nome:<28} {lim:%d/%m %H:%M}  {fmt(lim - stamps[0]):>11} {fmt(trabalho_ate(lim)):>9}")
