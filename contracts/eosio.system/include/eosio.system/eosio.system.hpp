#pragma once

#include <eosio/asset.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>

#include <eosio.system/exchange_state.hpp>
#include <eosio.system/native.hpp>

#include <eosio.proton/eosio.proton.hpp> //PROTON

#include <deque>
#include <optional>
#include <string>
#include <type_traits>

#ifdef CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX
#undef CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX
#endif
// CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX macro determines whether ramfee and namebid proceeds are
// channeled to REX pool. In order to stop these proceeds from being channeled, the macro must
// be set to 0.
#define CHANNEL_RAM_AND_NAMEBID_FEES_TO_REX 0

#define XPRsym symbol("XPR", 4)  // PROTON Main token 
#define SYSsym symbol("SYS", 4)  // PROTON System token for resource allocation

namespace eosiosystem {

   using eosio::asset;
   using eosio::block_timestamp;
   using eosio::check;
   using eosio::const_mem_fun;
   using eosio::datastream;
   using eosio::indexed_by;
   using eosio::name;
   using eosio::same_payer;
   using eosio::symbol;
   using eosio::symbol_code;
   using eosio::time_point;
   using eosio::time_point_sec;
   using eosio::unsigned_int;

   template<typename E, typename F>
   static inline auto has_field( F flags, E field )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, bool>
   {
      return ( (flags & static_cast<F>(field)) != 0 );
   }

   template<typename E, typename F>
   static inline auto set_field( F flags, E field, bool value = true )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, F >
   {
      if( value )
         return ( flags | static_cast<F>(field) );
      else
         return ( flags & ~static_cast<F>(field) );
   }

   static constexpr uint32_t seconds_per_year      = 52 * 7 * 24 * 3600;
   static constexpr uint32_t seconds_per_day       = 24 * 3600;
   static constexpr uint32_t seconds_per_hour      = 3600;
   static constexpr int64_t  useconds_per_year     = int64_t(seconds_per_year) * 1000'000ll;
   static constexpr int64_t  useconds_per_day      = int64_t(seconds_per_day) * 1000'000ll;
   static constexpr int64_t  useconds_per_hour     = int64_t(seconds_per_hour) * 1000'000ll;
   static constexpr uint32_t blocks_per_day        = 2 * seconds_per_day; // half seconds per day

   static constexpr int64_t  min_activated_stake   = 150'000'000'0000;
   static constexpr int64_t  ram_gift_bytes        = 1400;
   static constexpr int64_t  min_pervote_daily_pay = 100'0000;
   static constexpr uint32_t refund_delay_sec      = 0;               // PROTON no refund delay for SYS tokens, Old val: 3 * seconds_per_day;

   static constexpr int64_t  inflation_precision           = 100;     // 2 decimals
   static constexpr int64_t  default_annual_rate           = 500;     // 5% annual rate
   static constexpr int64_t  pay_factor_precision          = 10000;
   static constexpr int64_t  default_inflation_pay_factor  = 50000;   // producers pay share = 10000 / 50000 = 20% of the inflation
   static constexpr int64_t  default_votepay_factor        = 40000;   // per-block pay share = 10000 / 40000 = 25% of the producer pay


   /**
    * eosio.system contract
    * 
    * eosio.system contract defines the structures and actions needed for blockchain's core functionality.
    * - Users can stake tokens for CPU and Network bandwidth, and then vote for producers or
    *    delegate their vote to a proxy.
    * - Producers register in order to be voted for, and can claim per-block and per-vote rewards.
    * - Users can buy and sell RAM at a market-determined price.
    * - Users can bid on premium names.
    * - A resource exchange system (REX) allows token holders to lend their tokens,
    *    and users to rent CPU and Network resources in return for a market-determined fee.
    */

   // A name bid, which consists of:
   // - a `newname` name that the bid is for
   // - a `high_bidder` account name that is the one with the highest bid so far
   // - the `high_bid` which is amount of highest bid
   // - and `last_bid_time` which is the time of the highest bid
   struct [[eosio::table, eosio::contract("eosio.system")]] name_bid {
     name            newname;
     name            high_bidder;
     int64_t         high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
     time_point      last_bid_time;

     uint64_t primary_key()const { return newname.value;                    }
     uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
   };

   // A bid refund, which is defined by:
   // - the `bidder` account name owning the refund
   // - the `amount` to be refunded
   struct [[eosio::table, eosio::contract("eosio.system")]] bid_refund {
      name         bidder;
      asset        amount;

      uint64_t primary_key()const { return bidder.value; }
   };
   typedef eosio::multi_index< "namebids"_n, name_bid,
                               indexed_by<"highbid"_n, const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>  >
                             > name_bid_table;

   typedef eosio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;

   // Defines new global state parameters.
   struct [[eosio::table("global"), eosio::contract("eosio.system")]] eosio_global_state : eosio::blockchain_parameters {
      uint64_t free_ram()const { return max_ram_size - total_ram_bytes_reserved; }

      uint64_t             max_ram_size = 16ll*1024 * 1024 * 1024; //PROTON 64ll ->16ll   (16GB) 
      uint64_t             total_ram_bytes_reserved = 0;
      int64_t              total_ram_stake = 0;

      block_timestamp      last_producer_schedule_update;
      time_point           last_pervote_bucket_fill;
      int64_t              pervote_bucket = 0;
      int64_t              perblock_bucket = 0;
      uint32_t             total_unpaid_blocks = 0; /// all blocks which have been produced but not paid
      int64_t              total_activated_stake = 0;
      time_point           thresh_activated_stake_time;
      uint16_t             last_producer_schedule_size = 0;
      double               total_producer_vote_weight = 0; /// the sum of all producer votes
      block_timestamp      last_name_close;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE_DERIVED( eosio_global_state, eosio::blockchain_parameters,
                                (max_ram_size)(total_ram_bytes_reserved)(total_ram_stake)
                                (last_producer_schedule_update)(last_pervote_bucket_fill)
                                (pervote_bucket)(perblock_bucket)(total_unpaid_blocks)(total_activated_stake)(thresh_activated_stake_time)
                                (last_producer_schedule_size)(total_producer_vote_weight)(last_name_close) )
   };

   // Defines new global state parameters added after version 1.0
   struct [[eosio::table("global2"), eosio::contract("eosio.system")]] eosio_global_state2 {
      eosio_global_state2(){}

      uint16_t          new_ram_per_block = 0;
      block_timestamp   last_ram_increase;
      block_timestamp   last_block_num; /* deprecated */
      double            total_producer_votepay_share = 0;
      uint8_t           revision = 0; ///< used to track version updates in the future.

      EOSLIB_SERIALIZE( eosio_global_state2, (new_ram_per_block)(last_ram_increase)(last_block_num)
                        (total_producer_votepay_share)(revision) )
   };

   // Defines new global state parameters added after version 1.3.0
   struct [[eosio::table("global3"), eosio::contract("eosio.system")]] eosio_global_state3 {
      eosio_global_state3() { }
      time_point        last_vpay_state_update;
      double            total_vpay_share_change_rate = 0;

      EOSLIB_SERIALIZE( eosio_global_state3, (last_vpay_state_update)(total_vpay_share_change_rate) )
   };

   // Defines new global state parameters to store inflation rate and distribution
   struct [[eosio::table("global4"), eosio::contract("eosio.system")]] eosio_global_state4 {
      eosio_global_state4() { }
      double   continuous_rate;
      int64_t  inflation_pay_factor;
      int64_t  votepay_factor;

      EOSLIB_SERIALIZE( eosio_global_state4, (continuous_rate)(inflation_pay_factor)(votepay_factor) )
   };

   inline eosio::block_signing_authority convert_to_block_signing_authority( const eosio::public_key& producer_key ) {
      return eosio::block_signing_authority_v0{ .threshold = 1, .keys = {{producer_key, 1}} };
   }

   // Defines `producer_info` structure to be stored in `producer_info` table, added after version 1.0
   struct [[eosio::table, eosio::contract("eosio.system")]] producer_info {
      name                                                     owner;
      double                                                   total_votes = 0;
      eosio::public_key                                        producer_key; /// a packed public key object
      bool                                                     is_active = true;
      std::string                                              url;
      uint32_t                                                 unpaid_blocks = 0;
      time_point                                               last_claim_time;
      uint16_t                                                 location = 0;
      eosio::binary_extension<eosio::block_signing_authority>  producer_authority; // added in version 1.9.0

      uint64_t primary_key()const { return owner.value;                             }
      double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
      bool     active()const      { return is_active;                               }
      void     deactivate()       { producer_key = public_key(); producer_authority.reset(); is_active = false; }

      eosio::block_signing_authority get_producer_authority()const {
         if( producer_authority.has_value() ) {
            bool zero_threshold = std::visit( [](auto&& auth ) -> bool {
               return (auth.threshold == 0);
            }, *producer_authority );
            // zero_threshold could be true despite the validation done in regproducer2 because the v1.9.0 eosio.system
            // contract has a bug which may have modified the producer table such that the producer_authority field
            // contains a default constructed eosio::block_signing_authority (which has a 0 threshold and so is invalid).
            if( !zero_threshold ) return *producer_authority;
         }
         return convert_to_block_signing_authority( producer_key );
      }

      // The unregprod and claimrewards actions modify unrelated fields of the producers table and under the default
      // serialization behavior they would increase the size of the serialized table if the producer_authority field
      // was not already present. This is acceptable (though not necessarily desired) because those two actions require
      // the authority of the producer who pays for the table rows.
      // However, the rmvproducer action and the onblock transaction would also modify the producer table in a similar
      // way and increasing its serialized size is not acceptable in that context.
      // So, a custom serialization is defined to handle the binary_extension producer_authority
      // field in the desired way. (Note: v1.9.0 did not have this custom serialization behavior.)

      template<typename DataStream>
      friend DataStream& operator << ( DataStream& ds, const producer_info& t ) {
         ds << t.owner
            << t.total_votes
            << t.producer_key
            << t.is_active
            << t.url
            << t.unpaid_blocks
            << t.last_claim_time
            << t.location;

         if( !t.producer_authority.has_value() ) return ds;

         return ds << t.producer_authority;
      }

      template<typename DataStream>
      friend DataStream& operator >> ( DataStream& ds, producer_info& t ) {
         return ds >> t.owner
                   >> t.total_votes
                   >> t.producer_key
                   >> t.is_active
                   >> t.url
                   >> t.unpaid_blocks
                   >> t.last_claim_time
                   >> t.location
                   >> t.producer_authority;
      }
   };

   // Defines new producer info structure to be stored in new producer info table, added after version 1.3.0
   struct [[eosio::table, eosio::contract("eosio.system")]] producer_info2 {
      name            owner;
      double          votepay_share = 0;
      time_point      last_votepay_share_update;

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( producer_info2, (owner)(votepay_share)(last_votepay_share_update) )
   };

   // Voter info. Voter info stores information about the voter:
   // - `owner` the voter
   // - `proxy` the proxy set by the voter, if any
   // - `producers` the producers approved by this voter if no proxy set
   // - `staked` the amount staked
   struct [[eosio::table, eosio::contract("eosio.system")]] voter_info {
      name                owner;     /// the voter
      name                proxy;     /// the proxy set by the voter, if any
      std::vector<name>   producers; /// the producers approved by this voter if no proxy set
      int64_t             staked = 0;

      //  Every time a vote is cast we must first "undo" the last vote weight, before casting the
      //  new vote weight.  Vote weight is calculated as:
      //  stated.amount * 2 ^ ( weeks_since_launch/weeks_per_year)
      double              last_vote_weight = 0; /// the vote weight cast the last time the vote was updated

      // Total vote weight delegated to this voter.
      double              proxied_vote_weight= 0; /// the total vote weight delegated to this voter as a proxy
      bool                is_proxy = 0; /// whether the voter is a proxy for others


      uint32_t            flags1 = 0;
      uint32_t            reserved2 = 0;
      eosio::asset        reserved3;

      uint64_t primary_key()const { return owner.value; }

      enum class flags1_fields : uint32_t {
         ram_managed = 1,
         net_managed = 2,
         cpu_managed = 4
      };

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( voter_info, (owner)(proxy)(producers)(staked)(last_vote_weight)(proxied_vote_weight)(is_proxy)(flags1)(reserved2)(reserved3) )
   };


   typedef eosio::multi_index< "voters"_n, voter_info >  voters_table;


   typedef eosio::multi_index< "producers"_n, producer_info,
                               indexed_by<"prototalvote"_n, const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                             > producers_table;

   typedef eosio::multi_index< "producers2"_n, producer_info2 > producers_table2;


   typedef eosio::singleton< "global"_n, eosio_global_state >   global_state_singleton;

   typedef eosio::singleton< "global2"_n, eosio_global_state2 > global_state2_singleton;

   typedef eosio::singleton< "global3"_n, eosio_global_state3 > global_state3_singleton;
   
   typedef eosio::singleton< "global4"_n, eosio_global_state4 > global_state4_singleton;



   struct [[eosio::table, eosio::contract("eosio.system")]] user_resources {
      name          owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       ram_bytes = 0;

      bool is_empty()const { return net_weight.amount == 0 && cpu_weight.amount == 0 && ram_bytes == 0; }
      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(ram_bytes) )
   };

   
 
   // Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
   struct [[eosio::table, eosio::contract("eosio.system")]] delegated_bandwidth {
      name          from;
      name          to;
      asset         net_weight;
      asset         cpu_weight;

      bool is_empty()const { return net_weight.amount == 0 && cpu_weight.amount == 0; }
      uint64_t  primary_key()const { return to.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight) )

   };


   
   struct [[eosio::table, eosio::contract("eosio.system")]] refund_request {
      name            owner;
      time_point_sec  request_time;
      eosio::asset    net_amount;
      eosio::asset    cpu_amount;

      bool is_empty()const { return net_amount.amount == 0 && cpu_amount.amount == 0; }
      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( refund_request, (owner)(request_time)(net_amount)(cpu_amount) )
   };


   typedef eosio::multi_index< "userres"_n, user_resources >      user_resources_table;
   typedef eosio::multi_index< "delband"_n, delegated_bandwidth > del_bandwidth_table;
   typedef eosio::multi_index< "refunds"_n, refund_request >      refunds_table;

   
   // PROTON
   // Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
   struct [[eosio::table, eosio::contract("eosio.system")]] delegated_xpr {
      name                from;
      name                to;
      asset               quantity;


      bool is_empty()const { return quantity.amount == 0; }
      uint64_t  primary_key()const { return to.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( delegated_xpr, (from)(to)(quantity) )

   };
   typedef eosio::multi_index< "delxpr"_n, delegated_xpr > del_xpr_table;
   
   // PROTON
   struct [[eosio::table, eosio::contract("eosio.system")]] voters_xpr {
      name                owner;
      uint64_t            staked;
      bool                isqualified;
      uint64_t            claimamount;
      uint64_t            lastclaim;

      optional<uint64_t>  startstake;
      optional<bool>      startqualif;

      bool is_empty()const { return staked == 0; }
      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( voters_xpr, (owner)(staked)(isqualified)(claimamount)(lastclaim)(startstake)(startqualif) )

   };
   typedef eosio::multi_index< "votersxpr"_n, voters_xpr > voters_xpr_table;
   
   
   
   // PROTON
   struct [[eosio::table, eosio::contract("eosio.system")]] xpr_refund_request {
      name            owner;
      time_point_sec  request_time;
      eosio::asset    quantity;

      bool is_empty()const { return quantity.amount == 0; }
      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( xpr_refund_request, (owner)(request_time)(quantity) )
   };
   

   typedef eosio::multi_index< "refundsxpr"_n, xpr_refund_request >  xpr_refunds_table;

   
   // PROTON
   struct [[eosio::table("globalsxpr"), eosio::contract("eosio.system")]] eosio_global_statesxpr {
      eosio_global_statesxpr() { }
      uint64_t  max_bp_per_vote        = 4;                    // Max BPs allowed to vote from one account
      uint64_t  min_bp_reward          = 4;                    // Min voted BPs to get voter reward
      uint64_t  unstake_period         = 14 * 24 * 60 * 60;    // Unstake period for XPR tokens   14 * 24 * 60 * 60 = 14 days
      uint64_t  process_by             = 50;                   // How many accounts process in one step during voters reward sharing   
      uint64_t  process_interval       = 60 * 60 * 12;         // Time (sec) interval between voter reward sharing //      60 * 60 * 12;   = 12h 
      uint64_t  voters_claim_interval  = 60 * 60 * 24;         // Time (sec) between voter claim rewards (24h def)
      uint64_t  spare1                 = 0;
      uint64_t  spare2                 = 0;


      EOSLIB_SERIALIZE( eosio_global_statesxpr, (max_bp_per_vote)(min_bp_reward)(unstake_period)(process_by)(process_interval)(voters_claim_interval)(spare1)(spare2) )
   };
   typedef eosio::singleton< "globalsxpr"_n, eosio_global_statesxpr > global_statesxpr_singleton;

   // PROTON
   struct [[eosio::table("globalsd"), eosio::contract("eosio.system")]] eosio_global_statesd {
      eosio_global_statesd() { }
      
      int64_t  totalstaked    = 0;      // total staked amount  
      int64_t  totalrstaked   = 0;      // total staked amount by qualified voters 
      int64_t  totalrvoters   = 0;      // total voters who qualify for reward
      int64_t  notclaimed     = 0;      // reward not yet claimed by voters
      int64_t  pool           = 0;      // token pool (not distributed yet)

      int64_t  processtime    = 0;      // start distribution time. To keep track of distributuion process
      int64_t  processtimeupd = 0;      // next distribution range time. To keep track of distributuion process
      bool     isprocessing   = false;  // distribution is started.  
      name     processFrom    = ""_n;   // account name of last processed voter
      uint64_t processQuant   = 0 ;
      uint64_t processrstaked = 0 ;
      uint64_t processed      = 0; 

      int64_t  spare1 = 0; //--> processstaked
      int64_t  spare2 = 0;

      EOSLIB_SERIALIZE( eosio_global_statesd, (totalstaked)(totalrstaked)(totalrvoters)(notclaimed)(pool)(processtime)(processtimeupd)(isprocessing)(processFrom)(processQuant)(processrstaked)(processed)(spare1)(spare2) )
   };
   typedef eosio::singleton< "globalsd"_n, eosio_global_statesd > global_statesd_singleton;
   
   /*
   
   globalspr {
       poolspr
       processedspr
       processQspr

   */
   
   // `rex_pool` structure underlying the rex pool table. A rex pool table entry is defined by:
   // - `version` defaulted to zero,
   // - `total_lent` total amount of CORE_SYMBOL in open rex_loans
   // - `total_unlent` total amount of CORE_SYMBOL available to be lent (connector),
   // - `total_rent` fees received in exchange for lent  (connector),
   // - `total_lendable` total amount of CORE_SYMBOL that have been lent (total_unlent + total_lent),
   // - `total_rex` total number of REX shares allocated to contributors to total_lendable,
   // - `namebid_proceeds` the amount of CORE_SYMBOL to be transferred from namebids to REX pool,
   // - `loan_num` increments with each new loan
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_pool {
      uint8_t    version = 0;
      asset      total_lent;
      asset      total_unlent;
      asset      total_rent;
      asset      total_lendable;
      asset      total_rex;
      asset      namebid_proceeds;
      uint64_t   loan_num = 0;

      uint64_t primary_key()const { return 0; }
   };

   typedef eosio::multi_index< "rexpool"_n, rex_pool > rex_pool_table;

   // `rex_return_pool` structure underlying the rex return pool table. A rex return pool table entry is defined by:
   // - `version` defaulted to zero,
   // - `last_dist_time` the last time proceeds from renting, ram fees, and name bids were added to the rex pool,
   // - `pending_bucket_time` timestamp of the pending 12-hour return bucket,
   // - `oldest_bucket_time` cached timestamp of the oldest 12-hour return bucket,
   // - `pending_bucket_proceeds` proceeds in the pending 12-hour return bucket,
   // - `current_rate_of_increase` the current rate per dist_interval at which proceeds are added to the rex pool,
   // - `proceeds` the maximum amount of proceeds that can be added to the rex pool at any given time
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_return_pool {
      uint8_t        version = 0;
      time_point_sec last_dist_time;
      time_point_sec pending_bucket_time      = time_point_sec::maximum();
      time_point_sec oldest_bucket_time       = time_point_sec::min();
      int64_t        pending_bucket_proceeds  = 0;
      int64_t        current_rate_of_increase = 0;
      int64_t        proceeds                 = 0;

      static constexpr uint32_t total_intervals  = 30 * 144; // 30 days
      static constexpr uint32_t dist_interval    = 10 * 60;  // 10 minutes
      static constexpr uint8_t  hours_per_bucket = 12;
      static_assert( total_intervals * dist_interval == 30 * seconds_per_day );

      uint64_t primary_key()const { return 0; }
   };

   typedef eosio::multi_index< "rexretpool"_n, rex_return_pool > rex_return_pool_table;

   // `rex_return_buckets` structure underlying the rex return buckets table. A rex return buckets table is defined by:
   // - `version` defaulted to zero,
   // - `return_buckets` buckets of proceeds accumulated in 12-hour intervals
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_return_buckets {
      uint8_t                           version = 0;
      std::map<time_point_sec, int64_t> return_buckets;

      uint64_t primary_key()const { return 0; }
   };

   typedef eosio::multi_index< "retbuckets"_n, rex_return_buckets > rex_return_buckets_table;

   // `rex_fund` structure underlying the rex fund table. A rex fund table entry is defined by:
   // - `version` defaulted to zero,
   // - `owner` the owner of the rex fund,
   // - `balance` the balance of the fund.
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_fund {
      uint8_t version = 0;
      name    owner;
      asset   balance;

      uint64_t primary_key()const { return owner.value; }
   };

   typedef eosio::multi_index< "rexfund"_n, rex_fund > rex_fund_table;

   // `rex_balance` structure underlying the rex balance table. A rex balance table entry is defined by:
   // - `version` defaulted to zero,
   // - `owner` the owner of the rex fund,
   // - `vote_stake` the amount of CORE_SYMBOL currently included in owner's vote,
   // - `rex_balance` the amount of REX owned by owner,
   // - `matured_rex` matured REX available for selling
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_balance {
      uint8_t version = 0;
      name    owner;
      asset   vote_stake;
      asset   rex_balance;
      int64_t matured_rex = 0;
      std::deque<std::pair<time_point_sec, int64_t>> rex_maturities; /// REX daily maturity buckets

      uint64_t primary_key()const { return owner.value; }
   };

   typedef eosio::multi_index< "rexbal"_n, rex_balance > rex_balance_table;

   // `rex_loan` structure underlying the `rex_cpu_loan_table` and `rex_net_loan_table`. A rex net/cpu loan table entry is defined by:
   // - `version` defaulted to zero,
   // - `from` account creating and paying for loan,
   // - `receiver` account receiving rented resources,
   // - `payment` SYS tokens paid for the loan,
   // - `balance` is the amount of SYS tokens available to be used for loan auto-renewal,
   // - `total_staked` total amount staked,
   // - `loan_num` loan number/id,
   // - `expiration` the expiration time when loan will be either closed or renewed
   //       If payment <= balance, the loan is renewed, and closed otherwise.
   struct [[eosio::table,eosio::contract("eosio.system")]] rex_loan {
      uint8_t             version = 0;
      name                from;
      name                receiver;
      asset               payment;
      asset               balance;
      asset               total_staked;
      uint64_t            loan_num;
      eosio::time_point   expiration;

      uint64_t primary_key()const { return loan_num;                   }
      uint64_t by_expr()const     { return expiration.elapsed.count(); }
      uint64_t by_owner()const    { return from.value;                 }
   };

   typedef eosio::multi_index< "cpuloan"_n, rex_loan,
                               indexed_by<"byexpr"_n,  const_mem_fun<rex_loan, uint64_t, &rex_loan::by_expr>>,
                               indexed_by<"byowner"_n, const_mem_fun<rex_loan, uint64_t, &rex_loan::by_owner>>
                             > rex_cpu_loan_table;

   typedef eosio::multi_index< "netloan"_n, rex_loan,
                               indexed_by<"byexpr"_n,  const_mem_fun<rex_loan, uint64_t, &rex_loan::by_expr>>,
                               indexed_by<"byowner"_n, const_mem_fun<rex_loan, uint64_t, &rex_loan::by_owner>>
                             > rex_net_loan_table;

   struct [[eosio::table,eosio::contract("eosio.system")]] rex_order {
      uint8_t             version = 0;
      name                owner;
      asset               rex_requested;
      asset               proceeds;
      asset               stake_change;
      eosio::time_point   order_time;
      bool                is_open = true;

      void close()                { is_open = false;    }
      uint64_t primary_key()const { return owner.value; }
      uint64_t by_time()const     { return is_open ? order_time.elapsed.count() : std::numeric_limits<uint64_t>::max(); }
   };

   typedef eosio::multi_index< "rexqueue"_n, rex_order,
                               indexed_by<"bytime"_n, const_mem_fun<rex_order, uint64_t, &rex_order::by_time>>> rex_order_table;

   struct rex_order_outcome {
      bool success;
      asset proceeds;
      asset stake_change;
   };

   /**
    * The EOSIO system contract. The EOSIO system contract governs ram market, voters, producers, global state.
    */
   class [[eosio::contract("eosio.system")]] system_contract : public native {

      private:
         voters_table                  _voters;
         producers_table               _producers;
         producers_table2              _producers2;
         global_state_singleton        _global;
         global_state2_singleton       _global2;
         global_state3_singleton       _global3;
         global_state4_singleton       _global4;
         global_statesxpr_singleton    _globalsxpr;     // PROTON
         global_statesd_singleton      _globalsd;       // PROTON
         voters_xpr_table              vxpr_tbl;        // PROTON
         eosio_global_state            _gstate;
         eosio_global_state2           _gstate2;
         eosio_global_state3           _gstate3;
         eosio_global_state4           _gstate4;
         eosio_global_statesxpr        _gstatesxpr;     // PROTON
         eosio_global_statesd          _gstatesd;       // PROTON
         rammarket                     _rammarket;
         rex_pool_table                _rexpool;
         rex_return_pool_table         _rexretpool;
         rex_return_buckets_table      _rexretbuckets;
         rex_fund_table                _rexfunds;
         rex_balance_table             _rexbalance;
         rex_order_table               _rexorders;

      public:
         static constexpr eosio::name active_permission{"active"_n};
         static constexpr eosio::name proton_account{"eosio.proton"_n};       // PROTON
         static constexpr eosio::name cfund_account{"cfund.proton"_n};        // PROTON
         static constexpr eosio::name xpr_stake_account{"stake.proton"_n};    // PROTON
 
         static constexpr eosio::name token_account{"eosio.token"_n};
         static constexpr eosio::name ram_account{"eosio.ram"_n};
         static constexpr eosio::name ramfee_account{"eosio.ramfee"_n};
         static constexpr eosio::name stake_account{"eosio.stake"_n};
         static constexpr eosio::name bpay_account{"eosio.bpay"_n};
         static constexpr eosio::name vpay_account{"eosio.vpay"_n};
         static constexpr eosio::name names_account{"eosio.names"_n};
         static constexpr eosio::name saving_account{"eosio.saving"_n};

         static constexpr eosio::name rex_account{"eosio.rex"_n};
         static constexpr eosio::name null_account{"eosio.null"_n};
         static constexpr symbol ramcore_symbol = symbol(symbol_code("RAMCORE"), 4);
         static constexpr symbol ram_symbol     = symbol(symbol_code("RAM"), 0);
         static constexpr symbol rex_symbol     = symbol(symbol_code("REX"), 4);

         system_contract( name s, name code, datastream<const char*> ds );
         ~system_contract();

          // Returns the core symbol by system account name
          // @param system_account - the system account to get the core symbol for.
         static symbol get_core_symbol( name system_account = "eosio"_n ) {
            rammarket rm(system_account, system_account.value);
            const static auto sym = get_core_symbol( rm );
            return sym;
         }

         // Actions:
         /**
          * The Init action initializes the system contract for a version and a symbol.
          * Only succeeds when:
          * - version is 0 and
          * - symbol is found and
          * - system token supply is greater than 0,
          * - and system contract wasn’t already been initialized.
          *
          * @param version - the version, has to be 0,
          * @param core - the system symbol.
          */
         [[eosio::action]]
         void init( unsigned_int version, const symbol& core );

         /**
          * On block action. This special action is triggered when a block is applied by the given producer
          * and cannot be generated from any other source. It is used to pay producers and calculate
          * missed blocks of other producers. Producer pay is deposited into the producer's stake
          * balance and can be withdrawn over time. If blocknum is the start of a new round this may
          * update the active producer config from the producer votes.
          *
          * @param header - the block header produced.
          */
         [[eosio::action]]
         void onblock( ignore<block_header> header );

         /**
          * Set account limits action sets the resource limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param ram_bytes - ram limit in absolute bytes,
          * @param net_weight - fractionally proportionate net limit of available resources based on (weight / total_weight_of_all_accounts),
          * @param cpu_weight - fractionally proportionate cpu limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[eosio::action]]
         void setalimits( const name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight );

         /**
          * Set account RAM limits action, which sets the RAM limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param ram_bytes - ram limit in absolute bytes.
          */
         [[eosio::action]]
         void setacctram( const name& account, const std::optional<int64_t>& ram_bytes );

         /**
          * Set account NET limits action, which sets the NET limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param net_weight - fractionally proportionate net limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[eosio::action]]
         void setacctnet( const name& account, const std::optional<int64_t>& net_weight );

         /**
          * Set account CPU limits action, which sets the CPU limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param cpu_weight - fractionally proportionate cpu limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[eosio::action]]
         void setacctcpu( const name& account, const std::optional<int64_t>& cpu_weight );


         /**
          * The activate action, activates a protocol feature
          *
          * @param feature_digest - hash of the protocol feature to activate.
          */
         [[eosio::action]]
         void activate( const eosio::checksum256& feature_digest );

         // functions defined in delegate_bandwidth.cpp

         /**
          * Delegate bandwidth and/or cpu action. Stakes SYS from the balance of `from` for the benefit of `receiver`.
          *
          * @param from - the account to delegate bandwidth from, that is, the account holding
          *    tokens to be staked,
          * @param receiver - the account to delegate bandwith to, that is, the account to
          *    whose resources staked tokens are added
          * @param stake_net_quantity - tokens staked for NET bandwidth,
          * @param stake_cpu_quantity - tokens staked for CPU bandwidth,
          * @param transfer - if true, ownership of staked tokens is transfered to `receiver`.
          *
          * @post All producers `from` account has voted for will have their votes updated immediately.
          */
         [[eosio::action]]
         void delegatebw( const name& from, const name& receiver,
                          const asset& stake_net_quantity, const asset& stake_cpu_quantity, bool transfer );

         /**
          * Setrex action, sets total_rent balance of REX pool to the passed value.
          * @param balance - amount to set the REX pool balance.
          */
         [[eosio::action]]
         void setrex( const asset& balance );

         /**
          * Deposit to REX fund action. Deposits core tokens to user REX fund.
          * All proceeds and expenses related to REX are added to or taken out of this fund.
          * An inline transfer from 'owner' liquid balance is executed.
          * All REX-related costs and proceeds are deducted from and added to 'owner' REX fund,
          *    with one exception being buying REX using staked tokens.
          * Storage change is billed to 'owner'.
          *
          * @param owner - REX fund owner account,
          * @param amount - amount of tokens to be deposited.
          */
         [[eosio::action]]
         void deposit( const name& owner, const asset& amount );

         /**
          * Withdraw from REX fund action, withdraws core tokens from user REX fund.
          * An inline token transfer to user balance is executed.
          *
          * @param owner - REX fund owner account,
          * @param amount - amount of tokens to be withdrawn.
          */
         [[eosio::action]]
         void withdraw( const name& owner, const asset& amount );

         /**
          * Buyrex action, buys REX in exchange for tokens taken out of user's REX fund by transfering
          * core tokens from user REX fund and converts them to REX stake. By buying REX, user is
          * lending tokens in order to be rented as CPU or NET resourses.
          * Storage change is billed to 'from' account.
          *
          * @param from - owner account name,
          * @param amount - amount of tokens taken out of 'from' REX fund.
          *
          * @pre A voting requirement must be satisfied before action can be executed.
          * @pre User must vote for at least 21 producers or delegate vote to proxy before buying REX.
          *
          * @post User votes are updated following this action.
          * @post Tokens used in purchase are added to user's voting power.
          * @post Bought REX cannot be sold before 4 days counting from end of day of purchase.
          */
         [[eosio::action]]
         void buyrex( const name& from, const asset& amount );

         /**
          * Unstaketorex action, uses staked core tokens to buy REX.
          * Storage change is billed to 'owner' account.
          *
          * @param owner - owner of staked tokens,
          * @param receiver - account name that tokens have previously been staked to,
          * @param from_net - amount of tokens to be unstaked from NET bandwidth and used for REX purchase,
          * @param from_cpu - amount of tokens to be unstaked from CPU bandwidth and used for REX purchase.
          *
          * @pre A voting requirement must be satisfied before action can be executed.
          * @pre User must vote for at least 21 producers or delegate vote to proxy before buying REX.
          *
          * @post User votes are updated following this action.
          * @post Tokens used in purchase are added to user's voting power.
          * @post Bought REX cannot be sold before 4 days counting from end of day of purchase.
          */
         [[eosio::action]]
         void unstaketorex( const name& owner, const name& receiver, const asset& from_net, const asset& from_cpu );

         /**
          * Sellrex action, sells REX in exchange for core tokens by converting REX stake back into core tokens
          * at current exchange rate. If order cannot be processed, it gets queued until there is enough
          * in REX pool to fill order, and will be processed within 30 days at most. If successful, user
          * votes are updated, that is, proceeds are deducted from user's voting power. In case sell order
          * is queued, storage change is billed to 'from' account.
          *
          * @param from - owner account of REX,
          * @param rex - amount of REX to be sold.
          */
         [[eosio::action]]
         void sellrex( const name& from, const asset& rex );

         /**
          * Cnclrexorder action, cancels unfilled REX sell order by owner if one exists.
          *
          * @param owner - owner account name.
          *
          * @pre Order cannot be cancelled once it's been filled.
          */
         [[eosio::action]]
         void cnclrexorder( const name& owner );

         /**
          * Rentcpu action, uses payment to rent as many SYS tokens as possible as determined by market price and
          * stake them for CPU for the benefit of receiver, after 30 days the rented core delegation of CPU
          * will expire. At expiration, if balance is greater than or equal to `loan_payment`, `loan_payment`
          * is taken out of loan balance and used to renew the loan. Otherwise, the loan is closed and user
          * is refunded any remaining balance.
          * Owner can fund or refund a loan at any time before its expiration.
          * All loan expenses and refunds come out of or are added to owner's REX fund.
          *
          * @param from - account creating and paying for CPU loan, 'from' account can add tokens to loan
          *    balance using action `fundcpuloan` and withdraw from loan balance using `defcpuloan`
          * @param receiver - account receiving rented CPU resources,
          * @param loan_payment - tokens paid for the loan, it has to be greater than zero,
          *    amount of rented resources is calculated from `loan_payment`,
          * @param loan_fund - additional tokens can be zero, and is added to loan balance.
          *    Loan balance represents a reserve that is used at expiration for automatic loan renewal.
          */
         [[eosio::action]]
         void rentcpu( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );

         /**
          * Rentnet action, uses payment to rent as many SYS tokens as possible as determined by market price and
          * stake them for NET for the benefit of receiver, after 30 days the rented core delegation of NET
          * will expire. At expiration, if balance is greater than or equal to `loan_payment`, `loan_payment`
          * is taken out of loan balance and used to renew the loan. Otherwise, the loan is closed and user
          * is refunded any remaining balance.
          * Owner can fund or refund a loan at any time before its expiration.
          * All loan expenses and refunds come out of or are added to owner's REX fund.
          *
          * @param from - account creating and paying for NET loan, 'from' account can add tokens to loan
          *    balance using action `fundnetloan` and withdraw from loan balance using `defnetloan`,
          * @param receiver - account receiving rented NET resources,
          * @param loan_payment - tokens paid for the loan, it has to be greater than zero,
          *    amount of rented resources is calculated from `loan_payment`,
          * @param loan_fund - additional tokens can be zero, and is added to loan balance.
          *    Loan balance represents a reserve that is used at expiration for automatic loan renewal.
          */
         [[eosio::action]]
         void rentnet( const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );

         /**
          * Fundcpuloan action, transfers tokens from REX fund to the fund of a specific CPU loan in order to
          * be used for loan renewal at expiry.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param payment - tokens transfered from REX fund to loan fund.
          */
         [[eosio::action]]
         void fundcpuloan( const name& from, uint64_t loan_num, const asset& payment );

         /**
          * Fundnetloan action, transfers tokens from REX fund to the fund of a specific NET loan in order to
          * be used for loan renewal at expiry.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param payment - tokens transfered from REX fund to loan fund.
          */
         [[eosio::action]]
         void fundnetloan( const name& from, uint64_t loan_num, const asset& payment );

         /**
          * Defcpuloan action, withdraws tokens from the fund of a specific CPU loan and adds them to REX fund.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param amount - tokens transfered from CPU loan fund to REX fund.
          */
         [[eosio::action]]
         void defcpuloan( const name& from, uint64_t loan_num, const asset& amount );

         /**
          * Defnetloan action, withdraws tokens from the fund of a specific NET loan and adds them to REX fund.
          *
          * @param from - loan creator account,
          * @param loan_num - loan id,
          * @param amount - tokens transfered from NET loan fund to REX fund.
          */
         [[eosio::action]]
         void defnetloan( const name& from, uint64_t loan_num, const asset& amount );

         /**
          * Updaterex action, updates REX owner vote weight to current value of held REX tokens.
          *
          * @param owner - REX owner account.
          */
         [[eosio::action]]
         void updaterex( const name& owner );

         /**
          * Rexexec action, processes max CPU loans, max NET loans, and max queued sellrex orders.
          * Action does not execute anything related to a specific user.
          *
          * @param user - any account can execute this action,
          * @param max - number of each of CPU loans, NET loans, and sell orders to be processed.
          */
         [[eosio::action]]
         void rexexec( const name& user, uint16_t max );

         /**
          * Consolidate action, consolidates REX maturity buckets into one bucket that can be sold after 4 days
          * starting from the end of the day.
          *
          * @param owner - REX owner account name.
          */
         [[eosio::action]]
         void consolidate( const name& owner );

         /**
          * Mvtosavings action, moves a specified amount of REX into savings bucket. REX savings bucket
          * never matures. In order for it to be sold, it has to be moved explicitly
          * out of that bucket. Then the moved amount will have the regular maturity
          * period of 4 days starting from the end of the day.
          *
          * @param owner - REX owner account name.
          * @param rex - amount of REX to be moved.
          */
         [[eosio::action]]
         void mvtosavings( const name& owner, const asset& rex );

         /**
          * Mvfrsavings action, moves a specified amount of REX out of savings bucket. The moved amount
          * will have the regular REX maturity period of 4 days.
          *
          * @param owner - REX owner account name.
          * @param rex - amount of REX to be moved.
          */
         [[eosio::action]]
         void mvfrsavings( const name& owner, const asset& rex );

         /**
          * Closerex action, deletes owner records from REX tables and frees used RAM. Owner must not have
          * an outstanding REX balance.
          *
          * @param owner - user account name.
          *
          * @pre If owner has a non-zero REX balance, the action fails; otherwise,
          *    owner REX balance entry is deleted.
          * @pre If owner has no outstanding loans and a zero REX fund balance,
          *    REX fund entry is deleted.
          */
         [[eosio::action]]
         void closerex( const name& owner );


         // PROTON stake XPR
         /**
          * Stake XPR action
          *
          * @param from
          * @param receiver
          * @param stake_xpr_quantity
          */
         [[eosio::action]]
         void stakexpr ( const name& from, const name& receiver, const asset& stake_xpr_quantity);

         // PROTON unstake XPR
         /**
          * Unstake XPR action
          *
          * @param from
          * @param receiver
          * @param unstake_xpr_quantity
          */
         [[eosio::action]]
         void unstakexpr ( const name& from, const name& receiver, const asset& unstake_xpr_quantity);

         // PROTON refund XPR
         /**
          * Refund XPR action, this action is called after the delegation-period to claim all pending
          * unstaked tokens belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[eosio::action]]
         void refundxpr( const name& owner );

         // PROTON stake XPR
         /**
          * Voter Claim rewards action, claims voter reward.
          * @param owner - voter who voted for 4 bps.
          */
         [[eosio::action]]
         void voterclaim( const name& owner );

         // PROTON stake XPR
         /**
          * Voter Claim rewards action, claims voter reward and stake it.
          * @param owner - voter who voted for 4 bps.
          */
         [[eosio::action]]
         void voterclaimst( const name& owner );
 
 
         // PROTON process sharing vote rewards 
         /**
          * Voters reward sharing. Internal action.
          */
         [[eosio::action]]
         void vrwrdsharing( );

         // PROTON set voting system config
         /**
          * Set XPR votting module config
          * @param max_bp_per_vote
          * @param min_bp_reward
          * @param unstake_period
          * @param process_by
          * @param process_interval
          * @param voters_claim_interval
          */
         [[eosio::action]]
         void setxprvconf( const uint64_t&  max_bp_per_vote, const uint64_t& min_bp_reward, const uint64_t& unstake_period, const uint64_t& process_by, const uint64_t& process_interval, const uint64_t& voters_claim_interval );

         /**
          * Vote producer action, votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account. If voting for a `proxy`, the producer votes will not change until the
          * proxy updates their own vote. Voter can vote for a proxy __or__ a list of at most 30 producers.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param proxy - the proxy to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre If proxy is set then no producers can be voted for
          * @pre If proxy is set then proxy account must exist and be registered as a proxy
          * @pre Every listed producer or proxy must have been previously registered
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some EOS for voting
          * @pre Voter->staked must be up to date
          *
          * @post Every producer previously voted for will have vote reduced by previous vote weight
          * @post Every producer newly voted for will have vote increased by new vote amount
          * @post Prior proxy will proxied_vote_weight decremented by previous vote weight
          * @post New proxy will proxied_vote_weight incremented by new vote weight
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const name& proxy, const std::vector<name>& producers );


         /**
          * Undelegate bandwitdh action, decreases the total tokens delegated by `from` to `receiver` and/or
          * frees the memory associated with the delegation if there is nothing
          * left to delegate.
          * This will cause an immediate reduction in net/cpu bandwidth of the
          * receiver.
          * A transaction is scheduled to send the tokens back to `from` after
          * the staking period has passed. If existing transaction is scheduled, it
          * will be canceled and a new transaction issued that has the combined
          * undelegated amount.
          * The `from` account loses voting power as a result of this call and
          * all producer tallies are updated.
          *
          * @param from - the account to undelegate bandwidth from, that is,
          *    the account whose tokens will be unstaked,
          * @param receiver - the account to undelegate bandwith to, that is,
          *    the account to whose benefit tokens have been staked,
          * @param unstake_net_quantity - tokens to be unstaked from NET bandwidth,
          * @param unstake_cpu_quantity - tokens to be unstaked from CPU bandwidth,
          *
          * @post Unstaked tokens are transferred to `from` liquid balance via a
          *    deferred transaction with a delay of 3 days.
          * @post If called during the delay period of a previous `undelegatebw`
          *    action, pending action is canceled and timer is reset.
          * @post All producers `from` account has voted for will have their votes updated immediately.
          * @post Bandwidth and storage for the deferred transaction are billed to `from`.
          */
         [[eosio::action]]
         void undelegatebw( const name& from, const name& receiver,
                            const asset& unstake_net_quantity, const asset& unstake_cpu_quantity );

         /**
          * Buy ram action, increases receiver's ram quota based upon current price and quantity of
          * tokens provided. An inline transfer from receiver to system contract of
          * tokens will be executed.
          *
          * @param payer - the ram buyer,
          * @param receiver - the ram receiver,
          * @param quant - the quntity of tokens to buy ram with.
          */
         [[eosio::action]]
         void buyram( const name& payer, const name& receiver, const asset& quant );

         /**
          * Buy a specific amount of ram bytes action. Increases receiver's ram in quantity of bytes provided.
          * An inline transfer from receiver to system contract of tokens will be executed.
          *
          * @param payer - the ram buyer,
          * @param receiver - the ram receiver,
          * @param bytes - the quntity of ram to buy specified in bytes.
          */
         [[eosio::action]]
         void buyrambytes( const name& payer, const name& receiver, uint32_t bytes );

         /**
          * Sell ram action, reduces quota by bytes and then performs an inline transfer of tokens
          * to receiver based upon the average purchase price of the original quota.
          *
          * @param account - the ram seller account,
          * @param bytes - the amount of ram to sell in bytes.
          */
         [[eosio::action]]
         void sellram( const name& account, int64_t bytes );

         /**
          * Refund action, this action is called after the delegation-period to claim all pending
          * unstaked tokens belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[eosio::action]]
         void refund( const name& owner );

         // functions defined in voting.cpp

         /**
          * Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_key - the public key of the block producer, this is the key used by block producer to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer( const name& producer, const public_key& producer_key, const std::string& url, uint16_t location );

         /**
          * Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_authority - the weighted threshold multisig block signing authority of the block producer used to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer2( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location );

         /**
          * Unregister producer action, deactivates the block producer with account name `producer`.
          *
          * Deactivate the block producer with account name `producer`.
          * @param producer - the block producer account to unregister.
          */
         [[eosio::action]]
         void unregprod( const name& producer );

         
         // PROTON
         /**
          * Kick producer action, deactivates the block producer with account name `producer` and remove permission for regproducer.
          *
          * Deactivate the block producer with account name `producer` and remove permission for regproducer.
          * @param producer - the block producer account to unregister.
          */
         [[eosio::action]]
         void kickbp( const name& producer );
         
         /**
          * Set ram action sets the ram supply.
          * @param max_ram_size - the amount of ram supply to set.
          */
         [[eosio::action]]
         void setram( uint64_t max_ram_size );

         /**
          * Set ram rate action, sets the rate of increase of RAM in bytes per block. It is capped by the uint16_t to
          * a maximum rate of 3 TB per year. If update_ram_supply hasn't been called for the most recent block,
          * then new ram will be allocated at the old rate up to the present block before switching the rate.
          *
          * @param bytes_per_block - the amount of bytes per block increase to set.
          */
         [[eosio::action]]
         void setramrate( uint16_t bytes_per_block );

         /**
          * Vote producer action, votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account. If voting for a `proxy`, the producer votes will not change until the
          * proxy updates their own vote. Voter can vote for a proxy __or__ a list of at most 30 producers.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param proxy - the proxy to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre If proxy is set then no producers can be voted for
          * @pre If proxy is set then proxy account must exist and be registered as a proxy
          * @pre Every listed producer or proxy must have been previously registered
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some EOS for voting
          * @pre Voter->staked must be up to date
          *
          * @post Every producer previously voted for will have vote reduced by previous vote weight
          * @post Every producer newly voted for will have vote increased by new vote amount
          * @post Prior proxy will proxied_vote_weight decremented by previous vote weight
          * @post New proxy will proxied_vote_weight incremented by new vote weight
          */
         [[eosio::action]]
         void voteprodsys( const name& voter, const name& proxy, const std::vector<name>& producers );

         /**
          * Register proxy action, sets `proxy` account as proxy.
          * An account marked as a proxy can vote with the weight of other accounts which
          * have selected it as a proxy. Other accounts must refresh their voteproducer to
          * update the proxy's weight.
          * Storage change is billed to `proxy`.
          *
          * @param rpoxy - the account registering as voter proxy (or unregistering),
          * @param isproxy - if true, proxy is registered; if false, proxy is unregistered.
          *
          * @pre Proxy must have something staked (existing row in voters table)
          * @pre New state must be different than current state
          */
         [[eosio::action]]
         void regproxy( const name& proxy, bool isproxy );

         /**
          * Set the blockchain parameters. By tunning these parameters a degree of
          * customization can be achieved.
          * @param params - New blockchain parameters to set.
          */
         [[eosio::action]]
         void setparams( const eosio::blockchain_parameters& params );

         /**
          * Claim rewards action, claims block producing and vote rewards.
          * @param owner - producer account claiming per-block and per-vote rewards.
          */
         [[eosio::action]]
         void claimrewards( const name& owner );

         /**
          * Set privilege status for an account. Allows to set privilege status for an account (turn it on/off).
          * @param account - the account to set the privileged status for.
          * @param is_priv - 0 for false, > 0 for true.
          */
         [[eosio::action]]
         void setpriv( const name& account, uint8_t is_priv );

         /**
          * Remove producer action, deactivates a producer by name, if not found asserts.
          * @param producer - the producer account to deactivate.
          */
         [[eosio::action]]
         void rmvproducer( const name& producer );

         /**
          * Update revision action, updates the current revision.
          * @param revision - it has to be incremented by 1 compared with current revision.
          *
          * @pre Current revision can not be higher than 254, and has to be smaller
          * than or equal 1 (“set upper bound to greatest revision supported in the code”).
          */
         [[eosio::action]]
         void updtrevision( uint8_t revision );

         /**
          * Bid name action, allows an account `bidder` to place a bid for a name `newname`.
          * @param bidder - the account placing the bid,
          * @param newname - the name the bid is placed for,
          * @param bid - the amount of system tokens payed for the bid.
          *
          * @pre Bids can be placed only on top-level suffix,
          * @pre Non empty name,
          * @pre Names longer than 12 chars are not allowed,
          * @pre Names equal with 12 chars can be created without placing a bid,
          * @pre Bid has to be bigger than zero,
          * @pre Bid's symbol must be system token,
          * @pre Bidder account has to be different than current highest bidder,
          * @pre Bid must increase current bid by 10%,
          * @pre Auction must still be opened.
          */
         [[eosio::action]]
         void bidname( const name& bidder, const name& newname, const asset& bid );

         /**
          * Bid refund action, allows the account `bidder` to get back the amount it bid so far on a `newname` name.
          *
          * @param bidder - the account that gets refunded,
          * @param newname - the name for which the bid was placed and now it gets refunded for.
          */
         [[eosio::action]]
         void bidrefund( const name& bidder, const name& newname );

         /**
          * Change the annual inflation rate of the core token supply and specify how
          * the new issued tokens will be distributed based on the following structure.
          *
          * @param annual_rate - Annual inflation rate of the core token supply.
          *     (eg. For 5% Annual inflation => annual_rate=500
          *          For 1.5% Annual inflation => annual_rate=150
          * @param inflation_pay_factor - Inverse of the fraction of the inflation used to reward block producers.
          *     The remaining inflation will be sent to the `eosio.saving` account.
          *     (eg. For 20% of inflation going to block producer rewards   => inflation_pay_factor = 50000
          *          For 100% of inflation going to block producer rewards  => inflation_pay_factor = 10000).
          * @param votepay_factor - Inverse of the fraction of the block producer rewards to be distributed proportional to blocks produced.
          *     The remaining rewards will be distributed proportional to votes received.
          *     (eg. For 25% of block producer rewards going towards block pay => votepay_factor = 40000
          *          For 75% of block producer rewards going towards block pay => votepay_factor = 13333).
          */
         [[eosio::action]]
         void setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor );

         using init_action = eosio::action_wrapper<"init"_n, &system_contract::init>;
         using setacctram_action = eosio::action_wrapper<"setacctram"_n, &system_contract::setacctram>;
         using setacctnet_action = eosio::action_wrapper<"setacctnet"_n, &system_contract::setacctnet>;
         using setacctcpu_action = eosio::action_wrapper<"setacctcpu"_n, &system_contract::setacctcpu>;
         using activate_action = eosio::action_wrapper<"activate"_n, &system_contract::activate>;
         using delegatebw_action = eosio::action_wrapper<"delegatebw"_n, &system_contract::delegatebw>;
         using deposit_action = eosio::action_wrapper<"deposit"_n, &system_contract::deposit>;
         using withdraw_action = eosio::action_wrapper<"withdraw"_n, &system_contract::withdraw>;
         using buyrex_action = eosio::action_wrapper<"buyrex"_n, &system_contract::buyrex>;
         using unstaketorex_action = eosio::action_wrapper<"unstaketorex"_n, &system_contract::unstaketorex>;
         using sellrex_action = eosio::action_wrapper<"sellrex"_n, &system_contract::sellrex>;
         using cnclrexorder_action = eosio::action_wrapper<"cnclrexorder"_n, &system_contract::cnclrexorder>;
         using rentcpu_action = eosio::action_wrapper<"rentcpu"_n, &system_contract::rentcpu>;
         using rentnet_action = eosio::action_wrapper<"rentnet"_n, &system_contract::rentnet>;
         using fundcpuloan_action = eosio::action_wrapper<"fundcpuloan"_n, &system_contract::fundcpuloan>;
         using fundnetloan_action = eosio::action_wrapper<"fundnetloan"_n, &system_contract::fundnetloan>;
         using defcpuloan_action = eosio::action_wrapper<"defcpuloan"_n, &system_contract::defcpuloan>;
         using defnetloan_action = eosio::action_wrapper<"defnetloan"_n, &system_contract::defnetloan>;
         using updaterex_action = eosio::action_wrapper<"updaterex"_n, &system_contract::updaterex>;
         using rexexec_action = eosio::action_wrapper<"rexexec"_n, &system_contract::rexexec>;
         using setrex_action = eosio::action_wrapper<"setrex"_n, &system_contract::setrex>;
         using mvtosavings_action = eosio::action_wrapper<"mvtosavings"_n, &system_contract::mvtosavings>;
         using mvfrsavings_action = eosio::action_wrapper<"mvfrsavings"_n, &system_contract::mvfrsavings>;
         using consolidate_action = eosio::action_wrapper<"consolidate"_n, &system_contract::consolidate>;
         using closerex_action = eosio::action_wrapper<"closerex"_n, &system_contract::closerex>;
         using undelegatebw_action = eosio::action_wrapper<"undelegatebw"_n, &system_contract::undelegatebw>;
         using buyram_action = eosio::action_wrapper<"buyram"_n, &system_contract::buyram>;
         using buyrambytes_action = eosio::action_wrapper<"buyrambytes"_n, &system_contract::buyrambytes>;
         using sellram_action = eosio::action_wrapper<"sellram"_n, &system_contract::sellram>;
         using refund_action = eosio::action_wrapper<"refund"_n, &system_contract::refund>;
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &system_contract::regproducer>;
         using regproducer2_action = eosio::action_wrapper<"regproducer2"_n, &system_contract::regproducer2>;
         using unregprod_action = eosio::action_wrapper<"unregprod"_n, &system_contract::unregprod>;
         using kickbp_action = eosio::action_wrapper<"kickbp"_n, &system_contract::kickbp>;                       // PROTON
         using setram_action = eosio::action_wrapper<"setram"_n, &system_contract::setram>;
         using setramrate_action = eosio::action_wrapper<"setramrate"_n, &system_contract::setramrate>;
         using voteprodsys_action = eosio::action_wrapper<"voteprodsys"_n, &system_contract::voteprodsys>;        // PROTON
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &system_contract::voteproducer>;
         using regproxy_action = eosio::action_wrapper<"regproxy"_n, &system_contract::regproxy>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
         using rmvproducer_action = eosio::action_wrapper<"rmvproducer"_n, &system_contract::rmvproducer>;
         using updtrevision_action = eosio::action_wrapper<"updtrevision"_n, &system_contract::updtrevision>;
         using bidname_action = eosio::action_wrapper<"bidname"_n, &system_contract::bidname>;
         using bidrefund_action = eosio::action_wrapper<"bidrefund"_n, &system_contract::bidrefund>;
         using setpriv_action = eosio::action_wrapper<"setpriv"_n, &system_contract::setpriv>;
         using setalimits_action = eosio::action_wrapper<"setalimits"_n, &system_contract::setalimits>;
         using setparams_action = eosio::action_wrapper<"setparams"_n, &system_contract::setparams>;
         using setinflation_action = eosio::action_wrapper<"setinflation"_n, &system_contract::setinflation>;

         using refundxpr_action = eosio::action_wrapper<"refundxpr"_n, &system_contract::refundxpr>;              // PROTON
         using stakexpr_action = eosio::action_wrapper<"stakexpr"_n, &system_contract::stakexpr>;                 // PROTON
         using unstakexpr_action = eosio::action_wrapper<"unstakexpr"_n, &system_contract::unstakexpr>;           // PROTON
         using voterclaim_action = eosio::action_wrapper<"voterclaim"_n, &system_contract::voterclaim>;           // PROTON
         using voterclaimst_action = eosio::action_wrapper<"voterclaim"_n, &system_contract::voterclaim>;         // PROTON
         using vrwrdsharing_action = eosio::action_wrapper<"vrwrdsharing"_n, &system_contract::vrwrdsharing>;     // PROTON


         static uint8_t checkPermission(name acc, std::string permission);                                        // PROTON
         [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity, string memo);    // PROTON
      private:
         // Implementation details:

         static symbol get_core_symbol( const rammarket& rm ) {
            auto itr = rm.find(ramcore_symbol.raw());
            check(itr != rm.end(), "system contract must first be initialized");
            return itr->quote.balance.symbol;
         }

         //defined in eosio.system.cpp
         static eosio_global_state get_default_parameters();
         static eosio_global_state4 get_default_inflation_parameters();
         symbol core_symbol()const;
         void update_ram_supply();

         // defined in rex.cpp
         void runrex( uint16_t max );
         void update_rex_pool();
         void update_resource_limits( const name& from, const name& receiver, int64_t delta_net, int64_t delta_cpu );
         void check_voting_requirement( const name& owner,
                                        const char* error_msg = "must vote for at least 21 producers or for a proxy before buying REX" )const;
         rex_order_outcome fill_rex_order( const rex_balance_table::const_iterator& bitr, const asset& rex );
         asset update_rex_account( const name& owner, const asset& proceeds, const asset& unstake_quant, bool force_vote_update = false );
         void channel_to_rex( const name& from, const asset& amount );
         void channel_namebid_to_rex( const int64_t highest_bid );
         template <typename T>
         int64_t rent_rex( T& table, const name& from, const name& receiver, const asset& loan_payment, const asset& loan_fund );
         template <typename T>
         void fund_rex_loan( T& table, const name& from, uint64_t loan_num, const asset& payment );
         template <typename T>
         void defund_rex_loan( T& table, const name& from, uint64_t loan_num, const asset& amount );
         void transfer_from_fund( const name& owner, const asset& amount );
         void transfer_to_fund( const name& owner, const asset& amount );
         bool rex_loans_available()const;
         bool rex_system_initialized()const { return _rexpool.begin() != _rexpool.end(); }
         bool rex_available()const { return rex_system_initialized() && _rexpool.begin()->total_rex.amount > 0; }
         static time_point_sec get_rex_maturity();
         asset add_to_rex_balance( const name& owner, const asset& payment, const asset& rex_received );
         asset add_to_rex_pool( const asset& payment );
         void add_to_rex_return_pool( const asset& fee );
         void process_rex_maturities( const rex_balance_table::const_iterator& bitr );
         void consolidate_rex_balance( const rex_balance_table::const_iterator& bitr,
                                       const asset& rex_in_sell_order );
         int64_t read_rex_savings( const rex_balance_table::const_iterator& bitr );
         void put_rex_savings( const rex_balance_table::const_iterator& bitr, int64_t rex );
         void update_rex_stake( const name& voter );

         void add_loan_to_rex_pool( const asset& payment, int64_t rented_tokens, bool new_loan );
         void remove_loan_from_rex_pool( const rex_loan& loan );
         template <typename Index, typename Iterator>
         int64_t update_renewed_loan( Index& idx, const Iterator& itr, int64_t rented_tokens );

         // defined in delegate_bandwidth.cpp
         void changebw( name from, const name& receiver, const asset& stake_net_quantity, const asset& stake_cpu_quantity, bool transfer );
         void updstakexpr( name from, const name& receiver, const asset& xpr_quantity );
         void update_voting_power( const name& voter, const asset& total_update );
         void update_xpr_voting_power( const name& from, const name& voter, const asset& total_update );  // PROTON
         std::string timeToWait( const uint64_t& time_in_seconds );

         // defined in voting.cpp
         void register_producer( const name& producer, const eosio::block_signing_authority& producer_authority, const std::string& url, uint16_t location );
         void update_elected_producers( const block_timestamp& timestamp );
         void update_votes( const name& voter, const name& proxy, const std::vector<name>& producers, bool voting );
         void propagate_weight_change( const voter_info& voter );
         double update_producer_votepay_share( const producers_table2::const_iterator& prod_itr,
                                               const time_point& ct,
                                               double shares_rate, bool reset_to_zero = false );
         double update_total_votepay_share( const time_point& ct,
                                            double additional_shares_delta = 0.0, double shares_rate_delta = 0.0 );

         void update_xpr_votes( const name& from, const name& voter, const name& proxy, const std::vector<name>& producers, bool voting );      // PROTON
         void check_vote_sharing(  ); // PROTON  voterawrd sharing check
         void voterclaim_internal( const name& owner, const bool& tostake );  // PROTON stake XPR


         template <auto system_contract::*...Ptrs>
         class registration {
            public:
               template <auto system_contract::*P, auto system_contract::*...Ps>
               struct for_each {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, args... );
                     for_each<Ps...>::call( this_contract, std::forward<Args>(args)... );
                  }
               };
               template <auto system_contract::*P>
               struct for_each<P> {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, std::forward<Args>(args)... );
                  }
               };

               template <typename... Args>
               constexpr void operator() ( Args&&... args )
               {
                  for_each<Ptrs...>::call( this_contract, std::forward<Args>(args)... );
               }

               system_contract* this_contract;
         };

         registration<&system_contract::update_rex_stake> vote_stake_updater{ this };
   };

}
