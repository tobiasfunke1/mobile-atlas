CREATE TABLE users (
  id uuid PRIMARY KEY,
  userinfo jsonb
);

CREATE TABLE sessions (
  id integer PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  user_id uuid NOT NULL REFERENCES users (id) ON DELETE CASCADE,
  created timestamp with time zone NOT NULL DEFAULT NOW(),
  expires timestamp with time zone NOT NULL,
  access_token text NOT NULL,
  refresh_token text,
  cookie bytea UNIQUE
);

CREATE TABLE preferences (
  user_id uuid PRIMARY KEY REFERENCES users (id) ON DELETE CASCADE,
  led boolean
);

CREATE TABLE probes (
  id uuid PRIMARY KEY,
  name varchar(1024),
  led boolean
);
