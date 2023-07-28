The api-port is best used with nginx and a unix socket:

```
readsb [...] --net-api-port unix:/run/readsb/api.sock
```

nginx location:
```
location /re-api/ {
    gzip on;
    proxy_http_version 1.1;
    proxy_max_temp_file_size 0;
    proxy_set_header Connection $http_connection;
    proxy_set_header Host $http_host;
    proxy_pass http://unix:/run/readsb/api.sock:/$is_args$args;
}
```


Which can then be queried like this:
```
curl --compressed -sS 'http://localhost/re-api/?box=-90,90,0,20' | jq
```

See json readme for more details on the query syntax
