#!/usr/bin/env bash

set -eu

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
  CREATE USER moat_dashboard PASSWORD 'hunter2';
  GRANT USAGE ON SCHEMA public TO moat_dashboard;
  GRANT SELECT ON ALL TABLES IN SCHEMA public TO moat_dashboard;
  ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO moat_dashboard;
  CREATE DATABASE moat_dashboard OWNER moat_dashboard;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
CREATE TYPE public.probestatustype AS ENUM (
    'online',
    'offline'
);
--ALTER TYPE public.probestatustype OWNER TO moat;

CREATE TYPE public.tokenaction AS ENUM (
    'action:1',
    'action:2',
    'action:3',
    'action:4'
);
--ALTER TYPE public.tokenaction OWNER TO moat;

SET default_tablespace = '';
SET default_table_access_method = heap;

CREATE TABLE public.mam_token_log (
    id integer NOT NULL,
    token_id integer,
    token_value character varying NOT NULL,
    scope text NOT NULL,
    "time" timestamp with time zone DEFAULT now() NOT NULL,
    action public.tokenaction NOT NULL
);
--ALTER TABLE public.mam_token_log OWNER TO moat;

CREATE SEQUENCE public.mam_token_log_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.mam_token_log_id_seq OWNER TO moat;
ALTER SEQUENCE public.mam_token_log_id_seq OWNED BY public.mam_token_log.id;

CREATE TABLE public.mam_tokens (
    id integer NOT NULL,
    token character varying,
    token_candidate character varying,
    mac text,
    scope text NOT NULL
);
--ALTER TABLE public.mam_tokens OWNER TO moat;

CREATE SEQUENCE public.mam_tokens_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.mam_tokens_id_seq OWNER TO moat;
ALTER SEQUENCE public.mam_tokens_id_seq OWNED BY public.mam_tokens.id;

CREATE TABLE public.moat_session_tokens (
    id integer NOT NULL,
    value bytea NOT NULL,
    token_id integer NOT NULL,
    scope text NOT NULL,
    probe_id uuid,
    provider_id uuid,
    CONSTRAINT single_client CHECK (((probe_id IS NULL) <> (provider_id IS NULL)))
);
--ALTER TABLE public.moat_session_tokens OWNER TO moat;

CREATE SEQUENCE public.moat_session_tokens_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.moat_session_tokens_id_seq OWNER TO moat;
ALTER SEQUENCE public.moat_session_tokens_id_seq OWNED BY public.moat_session_tokens.id;

CREATE TABLE public.moat_tokens (
    id integer NOT NULL,
    value bytea NOT NULL,
    allowed_scope text NOT NULL,
    expires timestamp without time zone,
    admin boolean DEFAULT false NOT NULL
);
--ALTER TABLE public.moat_tokens OWNER TO moat;

CREATE SEQUENCE public.moat_tokens_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.moat_tokens_id_seq OWNER TO moat;
ALTER SEQUENCE public.moat_tokens_id_seq OWNED BY public.moat_tokens.id;

CREATE TABLE public.probe (
    id uuid DEFAULT gen_random_uuid() NOT NULL,
    name text NOT NULL,
    token_id integer NOT NULL,
    country character varying(2),
    last_poll timestamp with time zone
);
--ALTER TABLE public.probe OWNER TO moat;

CREATE TABLE public.probe_service_startup_log (
    id integer NOT NULL,
    mac text NOT NULL,
    "timestamp" timestamp with time zone NOT NULL,
    probe_id uuid
);
--ALTER TABLE public.probe_service_startup_log OWNER TO moat;

CREATE SEQUENCE public.probe_service_startup_log_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.probe_service_startup_log_id_seq OWNER TO moat;
ALTER SEQUENCE public.probe_service_startup_log_id_seq OWNED BY public.probe_service_startup_log.id;

CREATE TABLE public.probe_status (
    id integer NOT NULL,
    probe_id uuid NOT NULL,
    active boolean NOT NULL,
    status public.probestatustype NOT NULL,
    begin timestamp with time zone NOT NULL,
    "end" timestamp with time zone NOT NULL
);
--ALTER TABLE public.probe_status OWNER TO moat;

CREATE SEQUENCE public.probe_status_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.probe_status_id_seq OWNER TO moat;
ALTER SEQUENCE public.probe_status_id_seq OWNED BY public.probe_status.id;

CREATE TABLE public.probe_system_information (
    id integer NOT NULL,
    probe_id uuid NOT NULL,
    "timestamp" timestamp with time zone NOT NULL,
    information jsonb
);
--ALTER TABLE public.probe_system_information OWNER TO moat;

CREATE SEQUENCE public.probe_system_information_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.probe_system_information_id_seq OWNER TO moat;
ALTER SEQUENCE public.probe_system_information_id_seq OWNED BY public.probe_system_information.id;

CREATE TABLE public.providers (
    id uuid DEFAULT gen_random_uuid() NOT NULL
);
--ALTER TABLE public.providers OWNER TO moat;

CREATE TABLE public.sims (
    id integer NOT NULL,
    iccid character varying,
    imsi character varying,
    public boolean DEFAULT false NOT NULL
);
--ALTER TABLE public.sims OWNER TO moat;

CREATE SEQUENCE public.sims_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.sims_id_seq OWNER TO moat;
ALTER SEQUENCE public.sims_id_seq OWNED BY public.sims.id;

CREATE TABLE public.token_sim_association_table (
    sim_id integer NOT NULL,
    token_id integer NOT NULL,
    provide boolean DEFAULT false NOT NULL,
    request boolean DEFAULT false NOT NULL
);
--ALTER TABLE public.token_sim_association_table OWNER TO moat;

CREATE TABLE public.wireguard_config (
    id integer NOT NULL,
    publickey text,
    register_time timestamp with time zone DEFAULT now() NOT NULL,
    ip text NOT NULL,
    allow_registration boolean DEFAULT false NOT NULL,
    token_id integer NOT NULL
);
--ALTER TABLE public.wireguard_config OWNER TO moat;

CREATE SEQUENCE public.wireguard_config_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.wireguard_config_id_seq OWNER TO moat;
ALTER SEQUENCE public.wireguard_config_id_seq OWNED BY public.wireguard_config.id;

CREATE TABLE public.wireguard_config_logs (
    id integer NOT NULL,
    mac text,
    token character varying NOT NULL,
    publickey text NOT NULL,
    register_time timestamp with time zone NOT NULL,
    ip text,
    successful boolean NOT NULL
);
--ALTER TABLE public.wireguard_config_logs OWNER TO moat;

CREATE SEQUENCE public.wireguard_config_logs_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;
--ALTER SEQUENCE public.wireguard_config_logs_id_seq OWNER TO moat;
ALTER SEQUENCE public.wireguard_config_logs_id_seq OWNED BY public.wireguard_config_logs.id;

ALTER TABLE ONLY public.mam_token_log ALTER COLUMN id SET DEFAULT nextval('public.mam_token_log_id_seq'::regclass);
ALTER TABLE ONLY public.mam_tokens ALTER COLUMN id SET DEFAULT nextval('public.mam_tokens_id_seq'::regclass);
ALTER TABLE ONLY public.moat_session_tokens ALTER COLUMN id SET DEFAULT nextval('public.moat_session_tokens_id_seq'::regclass);
ALTER TABLE ONLY public.moat_tokens ALTER COLUMN id SET DEFAULT nextval('public.moat_tokens_id_seq'::regclass);
ALTER TABLE ONLY public.probe_service_startup_log ALTER COLUMN id SET DEFAULT nextval('public.probe_service_startup_log_id_seq'::regclass);
ALTER TABLE ONLY public.probe_status ALTER COLUMN id SET DEFAULT nextval('public.probe_status_id_seq'::regclass);
ALTER TABLE ONLY public.probe_system_information ALTER COLUMN id SET DEFAULT nextval('public.probe_system_information_id_seq'::regclass);
ALTER TABLE ONLY public.sims ALTER COLUMN id SET DEFAULT nextval('public.sims_id_seq'::regclass);
ALTER TABLE ONLY public.wireguard_config ALTER COLUMN id SET DEFAULT nextval('public.wireguard_config_id_seq'::regclass);
ALTER TABLE ONLY public.wireguard_config_logs ALTER COLUMN id SET DEFAULT nextval('public.wireguard_config_logs_id_seq'::regclass);
ALTER TABLE ONLY public.mam_token_log
    ADD CONSTRAINT mam_token_log_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.mam_tokens
    ADD CONSTRAINT mam_tokens_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.moat_session_tokens
    ADD CONSTRAINT moat_session_tokens_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.moat_session_tokens
    ADD CONSTRAINT moat_session_tokens_value_key UNIQUE (value);
ALTER TABLE ONLY public.moat_tokens
    ADD CONSTRAINT moat_tokens_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.moat_tokens
    ADD CONSTRAINT moat_tokens_value_key UNIQUE (value);
ALTER TABLE ONLY public.probe
    ADD CONSTRAINT probe_name_key UNIQUE (name);
ALTER TABLE ONLY public.probe
    ADD CONSTRAINT probe_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.probe_service_startup_log
    ADD CONSTRAINT probe_service_startup_log_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.probe_status
    ADD CONSTRAINT probe_status_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.probe_system_information
    ADD CONSTRAINT probe_system_information_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.probe
    ADD CONSTRAINT probe_token_id_key UNIQUE (token_id);
ALTER TABLE ONLY public.providers
    ADD CONSTRAINT providers_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.sims
    ADD CONSTRAINT sims_iccid_key UNIQUE (iccid);
ALTER TABLE ONLY public.sims
    ADD CONSTRAINT sims_imsi_key UNIQUE (imsi);
ALTER TABLE ONLY public.sims
    ADD CONSTRAINT sims_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.token_sim_association_table
    ADD CONSTRAINT token_sim_association_table_pkey PRIMARY KEY (sim_id, token_id);
ALTER TABLE ONLY public.wireguard_config
    ADD CONSTRAINT wireguard_config_ip_key UNIQUE (ip);
ALTER TABLE ONLY public.wireguard_config_logs
    ADD CONSTRAINT wireguard_config_logs_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.wireguard_config
    ADD CONSTRAINT wireguard_config_pkey PRIMARY KEY (id);
ALTER TABLE ONLY public.wireguard_config
    ADD CONSTRAINT wireguard_config_token_id_key UNIQUE (token_id);

CREATE UNIQUE INDEX ix_mam_tokens_token ON public.mam_tokens USING btree (token);
CREATE UNIQUE INDEX ix_mam_tokens_token_candidate ON public.mam_tokens USING btree (token_candidate);
CREATE INDEX ix_probe_status_active ON public.probe_status USING btree (active);
CREATE INDEX ix_wireguard_config_publickey ON public.wireguard_config USING btree (publickey);

ALTER TABLE ONLY public.mam_token_log
    ADD CONSTRAINT mam_token_log_token_id_fkey FOREIGN KEY (token_id) REFERENCES public.mam_tokens(id);
ALTER TABLE ONLY public.moat_session_tokens
    ADD CONSTRAINT moat_session_tokens_probe_id_fkey FOREIGN KEY (probe_id) REFERENCES public.probe(id);
ALTER TABLE ONLY public.moat_session_tokens
    ADD CONSTRAINT moat_session_tokens_provider_id_fkey FOREIGN KEY (provider_id) REFERENCES public.providers(id);
ALTER TABLE ONLY public.moat_session_tokens
    ADD CONSTRAINT moat_session_tokens_token_id_fkey FOREIGN KEY (token_id) REFERENCES public.moat_tokens(id);
ALTER TABLE ONLY public.probe_service_startup_log
    ADD CONSTRAINT probe_service_startup_log_probe_id_fkey FOREIGN KEY (probe_id) REFERENCES public.probe(id);
ALTER TABLE ONLY public.probe_status
    ADD CONSTRAINT probe_status_probe_id_fkey FOREIGN KEY (probe_id) REFERENCES public.probe(id);
ALTER TABLE ONLY public.probe_system_information
    ADD CONSTRAINT probe_system_information_probe_id_fkey FOREIGN KEY (probe_id) REFERENCES public.probe(id);
ALTER TABLE ONLY public.probe
    ADD CONSTRAINT probe_token_id_fkey FOREIGN KEY (token_id) REFERENCES public.mam_tokens(id);
ALTER TABLE ONLY public.token_sim_association_table
    ADD CONSTRAINT token_sim_association_table_sim_id_fkey FOREIGN KEY (sim_id) REFERENCES public.sims(id);
ALTER TABLE ONLY public.token_sim_association_table
    ADD CONSTRAINT token_sim_association_table_token_id_fkey FOREIGN KEY (token_id) REFERENCES public.moat_tokens(id);
ALTER TABLE ONLY public.wireguard_config
    ADD CONSTRAINT wireguard_config_token_id_fkey FOREIGN KEY (token_id) REFERENCES public.mam_tokens(id);
EOSQL
