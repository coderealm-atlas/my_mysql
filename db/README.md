# Commands

```bash
dbmate new create_third_party_login
dbmate --env-file db/.env_local up
dbmate --env-file db/.env_local drop
```

```bash
dbmate --env-file db/.env_prod_local up
dbmate --env-file db/.env_prod_local drop
```

## Copy the db directory to the production server

```bash
rsync -avz --progress db -e ssh user@api.cjj365.cc:/opt/db-mate/
```

server side.

```bash
# cd /opt/db-mate
# dbmate --env-file db/.env_prod_local up
bash /opt/db-mate/db/prod_migrate.sh
```
