"""
rodar_sql.py — executa um arquivo .sql (ou uma consulta) no banco do Supabase.

Uso (dentro da pasta cloud/db):
    python rodar_sql.py init/01_schema.sql      # roda o arquivo inteiro (tudo ou nada)
    python rodar_sql.py -c "SELECT now();"      # roda uma consulta e mostra o resultado

Lê as credenciais do arquivo cloud/.env (variáveis ADMIN_DB_*). Esse arquivo
não vai para o Git. Requer: pip install --user "psycopg[binary]"
"""
import sys
from pathlib import Path

import psycopg


def ler_env(caminho: Path) -> dict:
    """Lê linhas CHAVE=valor do .env, ignorando comentários e linhas vazias."""
    dados = {}
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():  # utf-8-sig ignora BOM
        linha = linha.strip()
        if linha and not linha.startswith("#") and "=" in linha:
            chave, valor = linha.split("=", 1)
            dados[chave.strip()] = valor.strip()
    return dados


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2

    env = ler_env(Path(__file__).resolve().parent.parent / ".env")
    try:
        conn = psycopg.connect(
            host=env["ADMIN_DB_HOST"],
            port=env.get("ADMIN_DB_PORT", "5432"),
            dbname=env.get("ADMIN_DB_NAME", "postgres"),
            user=env["ADMIN_DB_USER"],
            password=env["ADMIN_DB_PASSWORD"],
            sslmode="require",
            connect_timeout=15,
        )
    except KeyError as e:
        print(f"Faltou a variável {e} no cloud/.env")
        return 1

    if args[0] == "-c":
        sql = args[1]
    else:
        sql = Path(args[0]).read_text(encoding="utf-8")

    # "with conn": se der erro no meio do arquivo, desfaz tudo (rollback).
    with conn:
        with conn.cursor() as cur:
            cur.execute(sql)
            if cur.description:  # a consulta devolveu linhas
                colunas = [c.name for c in cur.description]
                print(" | ".join(colunas))
                for linha in cur.fetchall():
                    print(" | ".join(str(v) for v in linha))
            else:
                print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
