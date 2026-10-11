// GENERATED FILE — do not edit.
// Produced by native/tools/extract-graphql-queries.mjs from
// src/server/gfn/lcarsGraphql.ts. Re-run the script after changing the
// queries in the TypeScript client.
#pragma once

// Main / LibrarySection panel query
inline constexpr const char* kGameSectionQuery = R"GRAPHQL(query GetGameSection($vpcId: String!, $locale: String!, $panelNames: [String]!) {
  panels(vpcId: $vpcId, language: $locale, names: $panelNames) {
    id
    name
    sections {
      id
      title
      renderDirectives
      seeMoreInfo {
        filterTileId
        title
        filterIds
        minTiles
        sortOrderId
      }
      items {
        __typename
        ...filterFields
        ...minimalGameFields
        ...marketingFields
      }
    }
  }
}

fragment filterFields on FilterItem {
  id
  title
  image
  filterIds
  sortOrderId
}

fragment minimalGameFields on GameItem {
  app {
    id
    images {
      TV_BANNER
      HERO_IMAGE
    }
    title
    itemMetadata {
      campaignIds
    }
    variants {
      id
      shortName
      appStore
      supportedControls
      minimumSizeInBytes
      gfn {
        library {
          status
          selected
          playStatus
          installed
          subscription
        }
        status
        stateDetails {
          ... on VariantGfnAutoPatchingMetadata {
            subType
            endTime
          }
          ... on VariantGfnManualPatchingMetadata {
            subType
            endTime
          }
          ... on VariantGfnMaintenanceMetadata {
            subType
          }
        }
      }
    }
    gfn {
      playabilityState
      minimumMembershipTierLabel
      catalogSkuStrings {
        SKU_BASED_TAG
      }
      playType
    }
  }
}

fragment marketingFields on MarketingItem {
  id
  title
  subTitle
  body
  images {
    MARQUEE_HERO_IMAGE
    HERO_IMAGE
  }
  action {
    uri
    label
    infoText
  }
  schedule {
    startTime
    endTime
  }
})GRAPHQL";

// Home marquee query
inline constexpr const char* kMarqueeQuery = R"GRAPHQL(query GetMarquee($vpcId: String!, $locale: String!, $panelNames: [String]!) {
  panels(vpcId: $vpcId, language: $locale, names: $panelNames) {
    id
    name
    sections {
      id
      title
      items {
        __typename
        ...marketingFields
        ...marqueeGameFields
      }
    }
  }
}

fragment marketingFields on MarketingItem {
  id
  title
  images {
    MARQUEE_HERO_IMAGE
    HERO_IMAGE
  }
  body
  action {
    uri
    label
    infoText
  }
  schedule {
    startTime
    endTime
  }
}

fragment marqueeGameFields on GameItem {
  app {
    id
    title
    publisherName
    contentRatings {
      categoryKey
      contentDescriptorKeys
      interactiveElementKeys
      type
    }
    marqueeScrimPrimaryRGB {
      r
      g
      b
    }
    images {
      GAME_LOGO
      MARQUEE_HERO_IMAGE
      HERO_IMAGE
    }
    itemMetadata {
      campaignIds
    }
    variants {
      id
      appStore
      supportedControls
    }
  }
})GRAPHQL";

// Library panel with playtime
inline constexpr const char* kLibrarySectionWithTimeQuery = R"GRAPHQL(query GetGameSection($vpcId: String!, $locale: String!, $panelNames: [String]!) {
  panels(vpcId: $vpcId, language: $locale, names: $panelNames) {
    id
    name
    sections {
      id
      title
      renderDirectives
      seeMoreInfo {
        filterTileId
        title
        filterIds
        minTiles
        sortOrderId
      }
      items {
        __typename
        ...filterFields
        ...minimalGameFields
      }
    }
  }
}

fragment filterFields on FilterItem {
  id
  title
  image
  filterIds
  sortOrderId
}

fragment minimalGameFields on GameItem {
  app {
    id
    images {
      TV_BANNER
      HERO_IMAGE
      KEY_ART
    }
    title
    itemMetadata {
      campaignIds
    }
    variants {
      id
      shortName
      appStore
      publisherName
      supportedControls
      gfn {
        library {
          status
          selected
          playStatus
          lastPlayedDate
          subscription
        }
        status
        stateDetails {
          ... on VariantGfnAutoPatchingMetadata {
            subType
            endTime
          }
          ... on VariantGfnManualPatchingMetadata {
            subType
            endTime
          }
          ... on VariantGfnMaintenanceMetadata {
            subType
          }
        }
      }
      contentRatings {
        categoryKey
        type
      }
    }
    gfn {
      playabilityState
      minimumMembershipTierLabel
      catalogSkuStrings {
        SKU_BASED_TAG
      }
    }
  }
})GRAPHQL";

// Filter and sort definitions
inline constexpr const char* kFilterGroupAndSortQuery = R"GRAPHQL(query GetFilterGroupAndSortOrderDefinitions($locale: String!) {
  filterGroupDefinitions(language: $locale) {
    id
    label
    filters {
      id
      label
      filters
    }
  }
  sortOrderDefinitions(language: $locale) {
    id
    label
    orderBy
  }
})GRAPHQL";

// Single app details
inline constexpr const char* kAppDataForAppIdQuery = R"GRAPHQL(query GetAppDataQueryForAppId($vpcId: String!, $locale: String!, $appIds: [String]!) {
  apps(vpcId: $vpcId, language: $locale, appIds: $appIds) {
    items {
      contentRatings {
        categoryKey
        contentDescriptorKeys
        interactiveElementKeys
        type
      }
      developerName
      id
      genres
      images {
        GAME_BOX_ART
        GAME_LOGO
        HERO_IMAGE
        SCREENSHOTS
        TV_BANNER
        KEY_ART
      }
      nvidiaTech {
        PHOTO_MODE
        FREESTYLE
        HIGHLIGHTS
      }
      title
      longDescription
      shortDescription
      maxLocalPlayers
      maxOnlinePlayers
      supportedControls
      publisherName
      itemMetadata {
        campaignIds
      }
      variants {
        appStore
        id
        shortName
        supportedControls
        storeUrl
        publisherName
        developerName
        subscriptions
        minimumSizeInBytes
        cloudSaveSupported
        gfn {
          library {
            installed
            status
            selected
            playStatus
            subscription
            lastPlayedDate
          }
          status
          features {
            ...feature
          }
        }
      }
      gfn {
        playabilityState
        minimumMembershipTierLabel
        catalogSkuStrings {
          SKU_BASED_TAG
          SKU_BASED_PLAYABILITY_TEXT
          SKU_BASED_UNPLAYABLE_DIALOG_HEADER
          SKU_BASED_UNPLAYABLE_DIALOG_BODY_UPGRADE
          SKU_BASED_UNPLAYABLE_DIALOG_BODY_UPGRADE_ECOMM_RESTRICTED
        }
        playType
      }
    }
  }
}

fragment feature on GfnSubscriptionFeature {
  __typename
  ... on GfnSubscriptionFeatureValue {
    key
    value
  }
  ... on GfnSubscriptionFeatureValueList {
    key
    values
  }
})GRAPHQL";

// Mark a store variant owned
inline constexpr const char* kAddOwnedVariantMutation = R"GRAPHQL(mutation AddOwnedVariant($cmsId: String!, $locale: String!) {
  addOwnedVariant(language: $locale, variantId: $cmsId) {
    app {
      id
    }
  }
})GRAPHQL";

inline const char* FullQueryText(const char* queryName) {
  struct Entry { const char* name; const char* text; };
  static constexpr Entry kEntries[] = {
      {"GameSection", kGameSectionQuery},
      {"Marquee", kMarqueeQuery},
      {"LibrarySectionWithTime", kLibrarySectionWithTimeQuery},
      {"FilterGroupAndSort", kFilterGroupAndSortQuery},
      {"AppDataForAppId", kAppDataForAppIdQuery},
      {"AddOwnedVariant", kAddOwnedVariantMutation},
  };
  for (const Entry& entry : kEntries) {
    if (entry.name == std::string_view(queryName)) return entry.text;
  }
  return nullptr;
}
