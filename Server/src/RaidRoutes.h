#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/DbClient.h>

#include <memory>

class JwtTokens;

void registerRaidRoutes(drogon::HttpAppFramework& app, const drogon::orm::DbClientPtr& client,
                        const std::shared_ptr<const JwtTokens>& tokens);
