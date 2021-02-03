create database sadb with owner 'sadbadmin';

\c sadb

create table public.users
(
	id serial
		primary key,
	password varchar(128)
		not null,
	last_login timestamp with time zone,
	username varchar
		not null unique,
	first_name varchar,
	last_name varchar,
	email varchar
		not null unique,
	active boolean,
	admin boolean,
	date_joined timestamp with time zone
		default CURRENT_TIMESTAMP
		not null
);

alter table public.users owner to sadbadmin;

create table public.groups
(
	id serial
		primary key,
	name varchar
		not null unique
);

alter table public.groups owner to sadbadmin;

create table public.members
(
	user_id integer
		references public.users
			on delete cascade
			deferrable initially deferred,
	group_id integer
		references public.groups
			on delete cascade
			deferrable initially deferred,
	primary key (user_id, group_id)
);

alter table public.members owner to sadbadmin;
